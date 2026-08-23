#include "platform/win32/win32_tsf_input.h"

#include "domain/utf8_text.h"
#include "platform/win32/utf8.h"

#include <msctf.h>
#include <olectl.h>
#include <textstor.h>

#include <algorithm>
#include <atomic>
#include <utility>

namespace gitman::win32 {
    namespace {
        // 한 codepoint가 UTF-16에서 차지하는 코드 단위 수다. BMP 밖이면 2다
        // (surrogate 쌍).
        [[nodiscard]] long utf16_units(const char32_t codepoint) noexcept
        {
            return codepoint >= 0x10000u ? 2 : 1;
        }
    } // namespace

    long acp_length(const std::u8string_view text) noexcept
    {
        return acp_from_utf8(text, text.size());
    }

    long acp_from_utf8(const std::u8string_view text, const std::size_t byte_offset) noexcept
    {
        const std::size_t limit { byte_offset < text.size() ? byte_offset : text.size() };
        long acp { 0 };
        std::size_t offset { 0 };
        while (offset < limit)
        {
            const utf8_decoded decoded { utf8_decode(text, offset) };
            acp += utf16_units(decoded.codepoint);
            offset += decoded.length;
        }
        return acp;
    }

    std::size_t utf8_from_acp(const std::u8string_view text, const long acp) noexcept
    {
        if (acp <= 0)
            return 0;
        long remaining { acp };
        std::size_t offset { 0 };
        while (offset < text.size())
        {
            const utf8_decoded decoded { utf8_decode(text, offset) };
            const long units { utf16_units(decoded.codepoint) };
            if (remaining < units)
                // surrogate 쌍 한가운데를 가리켰다. 그 문자의 시작으로 되돌린다.
                return offset;
            remaining -= units;
            offset += decoded.length;
            if (remaining == 0)
                return offset;
        }
        return text.size();
    }

    namespace {
        // 그림자 문서를 UTF-16으로 옮긴 사본이다. TSF는 ACP(UTF-16)로만 말하므로
        // 읽기·쓰기마다 변환하지 않도록 함께 들고 다닌다.
        [[nodiscard]] std::wstring to_utf16(const std::u8string_view text)
        {
            const utf_conversion_result<std::wstring> converted { utf8_to_utf16(text) };
            return converted.value.has_value() ? *converted.value : std::wstring {};
        }

        [[nodiscard]] std::u8string to_utf8(const std::wstring_view text)
        {
            const utf_conversion_result<std::u8string> converted { utf16_to_utf8(text) };
            return converted.value.has_value() ? *converted.value : std::u8string {};
        }

        // `ITextStoreACP2` 구현이다 (E2.4). 그림자 문서를 TSF에 열어 주고, TSF가 쓴
        // 결과를 session이 logic으로 흘려보낸다.
        //
        // 문서 하나만 산다 — 초점이 옮겨 가면 session이 내용을 갈아 끼우고 sink에
        // 알린다.
        class text_store final
            : public ITextStoreACP2
            , public ITfContextOwnerCompositionSink
        {
        public:
            explicit text_store(tsf_host& host) noexcept
                : host_ { &host }
            {}

            // --- IUnknown ---

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
            {
                if (ppv == nullptr)
                    return E_INVALIDARG;
                *ppv = nullptr;
                if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITextStoreACP2))
                    *ppv = static_cast<ITextStoreACP2*>(this);
                else if (IsEqualIID(riid, IID_ITfContextOwnerCompositionSink))
                    *ppv = static_cast<ITfContextOwnerCompositionSink*>(this);
                else
                    return E_NOINTERFACE;
                AddRef();
                return S_OK;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return static_cast<ULONG>(references_.fetch_add(1, std::memory_order_relaxed) + 1);
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                const long remaining { references_.fetch_sub(1, std::memory_order_acq_rel) - 1 };
                if (remaining == 0)
                    delete this;
                return static_cast<ULONG>(remaining);
            }

            // --- 앱 쪽 창구 ---

            [[nodiscard]] const shadow_document& document() const noexcept
            {
                return document_;
            }

            [[nodiscard]] bool composing() const noexcept
            {
                return composing_;
            }

            [[nodiscard]] std::optional<text_input_target> target() const noexcept
            {
                return target_;
            }

            // 초점이 옮겨 갔거나 logic이 새 글을 게시했다. 조합 중에는 부르지 않는다.
            void reset_document(const std::optional<text_input_target> target, shadow_document document)
            {
                const long old_length { acp_length(document_.text) };
                target_ = target;
                document_ = std::move(document);

                if (sink_ == nullptr)
                    return;
                TS_TEXTCHANGE change {};
                change.acpStart = 0;
                change.acpOldEnd = old_length;
                change.acpNewEnd = acp_length(document_.text);
                static_cast<void>(sink_->OnTextChange(0, &change));
                static_cast<void>(sink_->OnSelectionChange());
                static_cast<void>(sink_->OnLayoutChange(TS_LC_CHANGE, 0));
            }

            // 편집 session이 끝날 때마다 결과를 logic에 보낸다 (E2.3).
            void publish()
            {
                if (host_ == nullptr || target_.has_value() == false)
                    return;

                if (composing_)
                {
                    set_text_composition_intent intent {};
                    intent.target = *target_;
                    intent.text = document_.text;
                    intent.caret = document_.caret;
                    intent.composing_begin = utf8_from_acp(document_.text, composing_begin_);
                    intent.composing_end = utf8_from_acp(document_.text, composing_end_);
                    intent.composing = true;
                    host_->post_composition(std::move(intent));
                    return;
                }

                if (dirty_ == false)
                    return;
                dirty_ = false;

                // 조합 없이 TIP이 곧바로 쓴 글이다. 확정된 글이므로 초안으로 보낸다.
                edit_text_input_intent edit {};
                edit.target = *target_;
                edit.command = text_edit_command::replace_all;
                edit.text = document_.text;
                edit.offset = document_.caret;
                host_->post_edit(std::move(edit));
            }

            // 조합을 끝내며 확정된 글을 보낸다. session이 초점 상실·외부 갱신에서 부른다.
            void finish_composition()
            {
                if (composing_ == false)
                    return;
                composing_ = false;
                composing_begin_ = 0;
                composing_end_ = 0;
                dirty_ = true;
                publish();
                clear_composition_view();
            }

            void clear_composition_view()
            {
                if (host_ == nullptr || target_.has_value() == false)
                    return;
                set_text_composition_intent intent {};
                intent.target = *target_;
                intent.composing = false;
                host_->post_composition(std::move(intent));
            }

            // --- ITextStoreACP2 ---

            HRESULT STDMETHODCALLTYPE AdviseSink(REFIID riid, IUnknown* punk, const DWORD dwMask) override
            {
                if (punk == nullptr)
                    return E_INVALIDARG;
                if (IsEqualIID(riid, IID_ITextStoreACPSink) == false)
                    return TS_E_NOOBJECT;

                ITextStoreACPSink* sink { nullptr };
                if (FAILED(punk->QueryInterface(IID_ITextStoreACPSink, reinterpret_cast<void**>(&sink))))
                    return E_NOINTERFACE;
                if (sink_ != nullptr)
                    sink_->Release();
                sink_ = sink;
                sink_mask_ = dwMask;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown*) override
            {
                if (sink_ == nullptr)
                    return CONNECT_E_NOCONNECTION;
                sink_->Release();
                sink_ = nullptr;
                sink_mask_ = 0;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE RequestLock(const DWORD dwLockFlags, HRESULT* phrSession) override
            {
                if (phrSession == nullptr)
                    return E_INVALIDARG;
                if (sink_ == nullptr)
                {
                    *phrSession = E_FAIL;
                    return E_UNEXPECTED;
                }
                if (lock_flags_ != 0)
                {
                    // 재진입이다. 동기 요청은 거절하고, 비동기 요청은 지금 진행 중인
                    // session이 끝난 뒤로 미룬다.
                    if ((dwLockFlags & TS_LF_SYNC) != 0)
                    {
                        *phrSession = TS_E_SYNCHRONOUS;
                        return S_OK;
                    }
                    pending_lock_flags_ = dwLockFlags & TS_LF_READWRITE;
                    *phrSession = TS_S_ASYNC;
                    return S_OK;
                }

                lock_flags_ = dwLockFlags & TS_LF_READWRITE;
                *phrSession = sink_->OnLockGranted(lock_flags_);
                lock_flags_ = 0;

                // session 안에서 들어온 요청을 이어서 처리한다. 한 번만 이어 준다 —
                // 무한히 이어지면 UI thread가 잡힌다.
                if (pending_lock_flags_ != 0)
                {
                    const DWORD pending { pending_lock_flags_ };
                    pending_lock_flags_ = 0;
                    lock_flags_ = pending;
                    static_cast<void>(sink_->OnLockGranted(pending));
                    lock_flags_ = 0;
                }

                publish();
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS* pdcs) override
            {
                if (pdcs == nullptr)
                    return E_INVALIDARG;
                pdcs->dwDynamicFlags = 0;
                // 한 줄 평문이다. 숨은 글도 여러 선택도 없다.
                pdcs->dwStaticFlags = TS_SS_NOHIDDENTEXT;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE QueryInsert(const LONG acpTestStart, const LONG acpTestEnd, ULONG, LONG* pacpResultStart, LONG* pacpResultEnd) override
            {
                if (pacpResultStart == nullptr || pacpResultEnd == nullptr)
                    return E_INVALIDARG;
                const LONG end { acp_length(document_.text) };
                if (acpTestStart < 0 || acpTestEnd > end || acpTestStart > acpTestEnd)
                    return E_INVALIDARG;
                *pacpResultStart = acpTestStart;
                *pacpResultEnd = acpTestEnd;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetSelection(const ULONG ulIndex, const ULONG ulCount, TS_SELECTION_ACP* pSelection, ULONG* pcFetched) override
            {
                if (pcFetched == nullptr)
                    return E_INVALIDARG;
                *pcFetched = 0;
                if (read_locked() == false)
                    return TS_E_NOLOCK;
                if (ulCount == 0 || pSelection == nullptr)
                    return S_OK;
                if (ulIndex != 0 && ulIndex != TS_DEFAULT_SELECTION)
                    return E_INVALIDARG;

                const LONG anchor { acp_from_utf8(document_.text, document_.anchor) };
                const LONG caret { acp_from_utf8(document_.text, document_.caret) };
                pSelection[0].acpStart = std::min(anchor, caret);
                pSelection[0].acpEnd = std::max(anchor, caret);
                pSelection[0].style.ase = caret >= anchor ? TS_AE_END : TS_AE_START;
                pSelection[0].style.fInterimChar = FALSE;
                *pcFetched = 1;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE SetSelection(const ULONG ulCount, const TS_SELECTION_ACP* pSelection) override
            {
                if (write_locked() == false)
                    return TS_E_NOLOCK;
                if (ulCount == 0 || pSelection == nullptr)
                    return S_OK;

                const std::size_t start { utf8_from_acp(document_.text, pSelection[0].acpStart) };
                const std::size_t end { utf8_from_acp(document_.text, pSelection[0].acpEnd) };
                if (pSelection[0].style.ase == TS_AE_START)
                {
                    document_.anchor = end;
                    document_.caret = start;
                }
                else
                {
                    document_.anchor = start;
                    document_.caret = end;
                }
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetText(const LONG acpStart, const LONG acpEnd, WCHAR* pchPlain, const ULONG cchPlainReq, ULONG* pcchPlainRet, TS_RUNINFO* prgRunInfo,
                const ULONG cRunInfoReq, ULONG* pcRunInfoRet, LONG* pacpNext) override
            {
                if (pcchPlainRet == nullptr || pcRunInfoRet == nullptr || pacpNext == nullptr)
                    return E_INVALIDARG;
                *pcchPlainRet = 0;
                *pcRunInfoRet = 0;
                *pacpNext = acpStart;
                if (read_locked() == false)
                    return TS_E_NOLOCK;

                const std::wstring wide { to_utf16(document_.text) };
                const LONG length { static_cast<LONG>(wide.size()) };
                if (acpStart < 0 || acpStart > length)
                    return TS_E_INVALIDPOS;
                const LONG end { acpEnd < 0 || acpEnd > length ? length : acpEnd };
                if (end < acpStart)
                    return TS_E_INVALIDPOS;

                ULONG available { static_cast<ULONG>(end - acpStart) };
                if (pchPlain != nullptr && cchPlainReq > 0)
                {
                    if (available > cchPlainReq)
                        available = cchPlainReq;
                    std::copy_n(wide.begin() + acpStart, available, pchPlain);
                    *pcchPlainRet = available;
                }
                else
                    available = 0;

                if (prgRunInfo != nullptr && cRunInfoReq > 0)
                {
                    // 숨은 글이 없으므로 run은 언제나 하나다.
                    prgRunInfo[0].uCount = available;
                    prgRunInfo[0].type = TS_RT_PLAIN;
                    *pcRunInfoRet = 1;
                }
                *pacpNext = acpStart + static_cast<LONG>(available);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE SetText(DWORD, const LONG acpStart, const LONG acpEnd, const WCHAR* pchText, const ULONG cch, TS_TEXTCHANGE* pChange) override
            {
                if (write_locked() == false)
                    return TS_E_NOLOCK;
                if (pChange == nullptr)
                    return E_INVALIDARG;

                const LONG length { acp_length(document_.text) };
                if (acpStart < 0 || acpStart > length || acpEnd > length || acpEnd < acpStart)
                    return TS_E_INVALIDPOS;

                const std::size_t begin { utf8_from_acp(document_.text, acpStart) };
                const std::size_t finish { utf8_from_acp(document_.text, acpEnd) };
                const std::u8string inserted { to_utf8(std::wstring_view { pchText, cch }) };

                document_.text.replace(begin, finish - begin, inserted);
                document_.caret = begin + inserted.size();
                document_.anchor = document_.caret;
                dirty_ = true;

                pChange->acpStart = acpStart;
                pChange->acpOldEnd = acpEnd;
                pChange->acpNewEnd = acpStart + static_cast<LONG>(cch);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE InsertTextAtSelection(const DWORD dwFlags, const WCHAR* pchText, const ULONG cch, LONG* pacpStart, LONG* pacpEnd, TS_TEXTCHANGE* pChange) override
            {
                const LONG selection_begin { acp_from_utf8(document_.text, std::min(document_.caret, document_.anchor)) };
                const LONG selection_end { acp_from_utf8(document_.text, std::max(document_.caret, document_.anchor)) };

                if ((dwFlags & TS_IAS_QUERYONLY) != 0)
                {
                    if (read_locked() == false)
                        return TS_E_NOLOCK;
                    if (pacpStart != nullptr)
                        *pacpStart = selection_begin;
                    if (pacpEnd != nullptr)
                        *pacpEnd = selection_end;
                    return S_OK;
                }

                if (write_locked() == false)
                    return TS_E_NOLOCK;

                const std::size_t begin { std::min(document_.caret, document_.anchor) };
                const std::size_t finish { std::max(document_.caret, document_.anchor) };
                const std::u8string inserted { to_utf8(std::wstring_view { pchText, cch }) };

                document_.text.replace(begin, finish - begin, inserted);
                document_.caret = begin + inserted.size();
                document_.anchor = document_.caret;
                dirty_ = true;

                const LONG new_end { selection_begin + static_cast<LONG>(cch) };
                if (pacpStart != nullptr)
                    *pacpStart = selection_begin;
                if (pacpEnd != nullptr)
                    *pacpEnd = new_end;
                if (pChange != nullptr)
                {
                    pChange->acpStart = selection_begin;
                    pChange->acpOldEnd = selection_end;
                    pChange->acpNewEnd = new_end;
                }
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetEndACP(LONG* pacp) override
            {
                if (pacp == nullptr)
                    return E_INVALIDARG;
                if (read_locked() == false)
                    return TS_E_NOLOCK;
                *pacp = acp_length(document_.text);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* pvcView) override
            {
                if (pvcView == nullptr)
                    return E_INVALIDARG;
                // 한 줄 박스 하나라 view도 하나다.
                *pvcView = 0;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override
            {
                // 포인터로 조합 글 안을 찍는 기능은 두지 않는다. 한 줄 박스에서는
                // 얻는 것보다 어긋날 위험이 크다.
                return E_NOTIMPL;
            }

            HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie, const LONG acpStart, const LONG acpEnd, RECT* prc, BOOL* pfClipped) override
            {
                if (prc == nullptr || pfClipped == nullptr)
                    return E_INVALIDARG;
                *pfClipped = FALSE;
                if (read_locked() == false)
                    return TS_E_NOLOCK;
                if (host_ == nullptr)
                    return E_FAIL;

                const std::size_t begin { utf8_from_acp(document_.text, acpStart) };
                const std::size_t end { utf8_from_acp(document_.text, acpEnd) };
                const std::optional<RECT> rect { host_->text_screen_rect(begin, end) };
                if (rect.has_value() == false)
                    // 아직 배치를 모른다. TSF는 이 값을 보고 다시 묻는다.
                    return TS_E_NOLAYOUT;
                *prc = *rect;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie, RECT* prc) override
            {
                if (prc == nullptr)
                    return E_INVALIDARG;
                if (host_ == nullptr)
                    return E_FAIL;
                const std::optional<RECT> rect { host_->text_screen_rect(0, document_.text.size()) };
                if (rect.has_value() == false)
                    return TS_E_NOLAYOUT;
                *prc = *rect;
                return S_OK;
            }

            // 서식·임베드는 한 줄 평문 박스에 뜻이 없다.
            HRESULT STDMETHODCALLTYPE GetFormattedText(LONG, LONG, IDataObject**) override
            {
                return E_NOTIMPL;
            }

            HRESULT STDMETHODCALLTYPE GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override
            {
                return E_NOTIMPL;
            }

            HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* pfInsertable) override
            {
                if (pfInsertable == nullptr)
                    return E_INVALIDARG;
                *pfInsertable = FALSE;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override
            {
                return E_NOTIMPL;
            }

            HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override
            {
                return E_NOTIMPL;
            }

            // 속성은 지원하지 않는다. 요청은 받아 두고 조회에 0개를 돌려준다 —
            // 실패를 돌려주면 일부 TIP이 조합을 시작하지 못한다.
            HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG, LONG, ULONG, const TS_ATTRID*, DWORD, LONG* pacpNext, BOOL* pfFound, LONG* plFoundOffset) override
            {
                if (pacpNext == nullptr || pfFound == nullptr || plFoundOffset == nullptr)
                    return E_INVALIDARG;
                *pacpNext = 0;
                *pfFound = FALSE;
                *plFoundOffset = 0;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* pcFetched) override
            {
                if (pcFetched == nullptr)
                    return E_INVALIDARG;
                *pcFetched = 0;
                return S_OK;
            }

            // --- ITfContextOwnerCompositionSink ---

            HRESULT STDMETHODCALLTYPE OnStartComposition(ITfCompositionView* pComposition, BOOL* pfOk) override
            {
                if (pfOk != nullptr)
                    *pfOk = TRUE;
                composing_ = true;
                update_composing_range(pComposition);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView* pComposition, ITfRange*) override
            {
                update_composing_range(pComposition);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnEndComposition(ITfCompositionView*) override
            {
                // 확정된 글은 그림자에 이미 들어와 있다. 이제 초안으로 보낸다.
                composing_ = false;
                composing_begin_ = 0;
                composing_end_ = 0;
                dirty_ = true;
                publish();
                clear_composition_view();
                return S_OK;
            }

        private:
            ~text_store()
            {
                if (sink_ != nullptr)
                    sink_->Release();
            }

            [[nodiscard]] bool read_locked() const noexcept
            {
                return (lock_flags_ & TS_LF_READ) == TS_LF_READ;
            }

            [[nodiscard]] bool write_locked() const noexcept
            {
                return (lock_flags_ & TS_LF_READWRITE) == TS_LF_READWRITE;
            }

            // 조합 구간을 ACP로 다시 읽는다. 못 읽으면 직전 값을 그대로 둔다.
            void update_composing_range(ITfCompositionView* const view)
            {
                if (view == nullptr)
                    return;
                ITfRange* range { nullptr };
                if (FAILED(view->GetRange(&range)) || range == nullptr)
                    return;

                ITfRangeACP* range_acp { nullptr };
                if (SUCCEEDED(range->QueryInterface(IID_ITfRangeACP, reinterpret_cast<void**>(&range_acp))) && range_acp != nullptr)
                {
                    LONG start { 0 };
                    LONG count { 0 };
                    if (SUCCEEDED(range_acp->GetExtent(&start, &count)))
                    {
                        composing_begin_ = start;
                        composing_end_ = start + count;
                    }
                    range_acp->Release();
                }
                range->Release();
            }

            std::atomic<long> references_ { 1 };
            tsf_host* host_ { nullptr };
            ITextStoreACPSink* sink_ { nullptr };
            DWORD sink_mask_ { 0 };
            DWORD lock_flags_ { 0 };
            DWORD pending_lock_flags_ { 0 };

            std::optional<text_input_target> target_ {};
            shadow_document document_ {};
            // 그림자가 마지막 게시 이후 바뀌었다.
            bool dirty_ { false };
            bool composing_ { false };
            LONG composing_begin_ { 0 };
            LONG composing_end_ { 0 };
        };
    } // namespace

    class tsf_input_session::implementation
    {
    public:
        implementation(HWND window, tsf_host& host) noexcept
            : window_ { window }
            , host_ { &host }
        {}

        implementation(const implementation&) = delete;
        implementation(implementation&&) = delete;
        implementation& operator=(const implementation&) = delete;
        implementation& operator=(implementation&&) = delete;

        ~implementation()
        {
            if (store_ != nullptr)
                store_->finish_composition();
            if (thread_manager_ != nullptr)
            {
                static_cast<void>(thread_manager_->SetFocus(nullptr));
                if (document_manager_ != nullptr)
                    static_cast<void>(document_manager_->Pop(TF_POPF_ALL));
                static_cast<void>(thread_manager_->Deactivate());
            }
            if (keystroke_manager_ != nullptr)
                keystroke_manager_->Release();
            if (context_ != nullptr)
                context_->Release();
            if (document_manager_ != nullptr)
                document_manager_->Release();
            if (thread_manager_ != nullptr)
                thread_manager_->Release();
            if (store_ != nullptr)
                store_->Release();
        }

        [[nodiscard]] bool initialize()
        {
            if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr2, reinterpret_cast<void**>(&thread_manager_))) || thread_manager_ == nullptr)
                return false;
            if (FAILED(thread_manager_->ActivateEx(&client_id_, TF_TMAE_NOACTIVATETIP)))
                return false;
            if (FAILED(thread_manager_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keystroke_manager_))))
                keystroke_manager_ = nullptr;
            if (FAILED(thread_manager_->CreateDocumentMgr(&document_manager_)) || document_manager_ == nullptr)
                return false;

            store_ = new text_store { *host_ };
            TfEditCookie cookie { 0 };
            if (FAILED(document_manager_->CreateContext(client_id_, 0, static_cast<ITextStoreACP2*>(store_), &context_, &cookie)) || context_ == nullptr)
                return false;
            if (FAILED(document_manager_->Push(context_)))
                return false;
            return true;
        }

        void synchronize()
        {
            if (store_ == nullptr || thread_manager_ == nullptr)
                return;

            const std::optional<text_input_target> target { host_->focused_text_target() };
            if (target != store_->target())
            {
                // 초점이 옮겨 간다. 조합 중이었다면 확정하고 끝낸다.
                store_->finish_composition();
                store_->reset_document(target, target.has_value() ? host_->committed_document() : shadow_document {});
                static_cast<void>(thread_manager_->SetFocus(target.has_value() ? document_manager_ : nullptr));
                return;
            }
            if (target.has_value() == false)
                return;

            // 조합 도중 글이 밑에서 바뀌면 TIP이 혼란에 빠진다. 조합이 끝날 때까지
            // 미룬다 (E2.3).
            if (store_->composing())
                return;

            const shadow_document committed { host_->committed_document() };
            if (committed == store_->document())
                return;
            store_->reset_document(target, committed);
        }

        [[nodiscard]] bool handle_key(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
        {
            if (keystroke_manager_ == nullptr || store_ == nullptr || store_->target().has_value() == false)
                return false;

            BOOL eaten { FALSE };
            const bool down { message == WM_KEYDOWN };
            const HRESULT tested {
                down ? keystroke_manager_->TestKeyDown(word_parameter, long_parameter, &eaten) : keystroke_manager_->TestKeyUp(word_parameter, long_parameter, &eaten),
            };
            if (FAILED(tested) || eaten == FALSE)
                return false;

            eaten = FALSE;
            const HRESULT handled {
                down ? keystroke_manager_->KeyDown(word_parameter, long_parameter, &eaten) : keystroke_manager_->KeyUp(word_parameter, long_parameter, &eaten),
            };
            return SUCCEEDED(handled) && eaten != FALSE;
        }

        [[nodiscard]] bool composing() const noexcept
        {
            return store_ != nullptr && store_->composing();
        }

    private:
        HWND window_ { nullptr };
        tsf_host* host_ { nullptr };
        ITfThreadMgr2* thread_manager_ { nullptr };
        ITfKeystrokeMgr* keystroke_manager_ { nullptr };
        ITfDocumentMgr* document_manager_ { nullptr };
        ITfContext* context_ { nullptr };
        text_store* store_ { nullptr };
        TfClientId client_id_ { 0 };
    };

    std::unique_ptr<tsf_input_session> tsf_input_session::create(const HWND window, tsf_host& host)
    {
        auto impl { std::make_unique<implementation>(window, host) };
        if (impl->initialize() == false)
            return nullptr;
        return std::unique_ptr<tsf_input_session> { new tsf_input_session { std::move(impl) } };
    }

    tsf_input_session::tsf_input_session(std::unique_ptr<implementation> impl) noexcept
        : impl_ { std::move(impl) }
    {}

    tsf_input_session::~tsf_input_session() = default;

    void tsf_input_session::synchronize()
    {
        impl_->synchronize();
    }

    bool tsf_input_session::handle_key(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        return impl_->handle_key(message, word_parameter, long_parameter);
    }

    bool tsf_input_session::composing() const noexcept
    {
        return impl_->composing();
    }
} // namespace gitman::win32
