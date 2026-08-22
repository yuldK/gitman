#include "platform/win32/win32_fonts.h"

#include "domain/utf8_text.h"
#include "platform/win32/embedded_assets.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkTypeface_win.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace gitman::win32 {
    namespace {
        // 가족 이름 → typeface cache와 현재 설정을 함께 지키는 자물쇠다. 잠금 구간은
        // map 조회 하나라 입력 thread의 글자 폭 측정에 부담이 되지 않는다.
        struct font_registry
        {
            std::mutex mutex {};
            sk_sp<SkFontMgr> manager {};
            bool manager_ready { false };
            std::map<std::u8string, sk_sp<SkTypeface>, std::less<>> cache {};
            std::u8string ui_family {};
            std::u8string code_family {};
            sk_sp<SkTypeface> ui_typeface {};
            sk_sp<SkTypeface> code_typeface {};
        };

        font_registry& registry()
        {
            static font_registry instance {};
            return instance;
        }

        // 자물쇠를 이미 쥔 상태에서 부른다.
        //
        // **GDI가 아니라 DirectWrite다** (text-input-and-font-fallback-design E3.1).
        // Skia는 `UNICODE` 없이 빌드되어 GDI font manager가 ANSI API로 컴파일된다
        // (`EnumFontFamiliesExA`). 그러면 한국어 Windows에서 `맑은 고딕` 같은 이름이
        // CP949 바이트로 와서 UTF-8이 아니게 되고, 그리는 순간 Skia가 abort한다.
        // DirectWrite는 이름을 사용자 로캘의 UTF-8로 주고 글자 단위 대체(fallback)도
        // 지원한다. `embedded_assets`가 이미 쓰던 관리자와 같은 것이라 앱에 하나만 남는다.
        SkFontMgr* manager_locked(font_registry& state)
        {
            if (state.manager_ready == false)
            {
                state.manager_ready = true;
                state.manager = SkFontMgr_New_DirectWrite();
            }
            return state.manager.get();
        }

        sk_sp<SkTypeface> resolve_locked(font_registry& state, const std::u8string_view family)
        {
            // 내장 글꼴도 cache에 남긴다. 매번 새로 만들어 돌려주면 호출자가 raw
            // 포인터로 받는 순간(미리 보기 해석기) 수명이 끊긴다.
            if (const auto found { state.cache.find(family) }; found != state.cache.end())
                return found->second;
            if (family.empty())
            {
                sk_sp<SkTypeface> embedded { load_ui_typeface() };
                state.cache.emplace(std::u8string {}, embedded);
                return embedded;
            }

            sk_sp<SkTypeface> typeface {};
            if (SkFontMgr* const manager { manager_locked(state) }; manager != nullptr)
            {
                const std::string name { reinterpret_cast<const char*>(family.data()), family.size() };
                typeface = manager->matchFamilyStyle(name.c_str(), SkFontStyle::Normal());
            }
            // 찾지 못한 이름도 cache에 넣는다. 같은 이름으로 매 frame 다시 뒤지지 않는다.
            state.cache.emplace(std::u8string { family }, typeface);
            return typeface;
        }
    } // namespace

    std::vector<std::u8string> installed_font_families()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        SkFontMgr* const manager { manager_locked(state) };
        if (manager == nullptr)
            return {};

        std::vector<std::u8string> families {};
        const int count { manager->countFamilies() };
        families.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
        for (int index = 0; index < count; ++index)
        {
            SkString name {};
            manager->getFamilyName(index, &name);
            if (name.isEmpty())
                continue;

            // 관리자에 따라 길이에 끝의 NUL이 들어 있다 (GDI 경로의 `tchar_to_skstring`).
            // 남겨 두면 이름 뒤에 빈 글리프가 하나 붙는다.
            std::u8string family { reinterpret_cast<const char8_t*>(name.c_str()), name.size() };
            while (family.empty() == false && family.back() == u8'\0')
                family.pop_back();
            // 관리자를 바꿔도 방어는 남긴다. 잘못된 UTF-8 이름은 목록에 넣지 않는다 —
            // 그리는 순간 Skia가 abort하고, 어차피 해석도 되지 않는다 (E3.6).
            if (family.empty() || utf8_is_valid(family) == false)
                continue;
            families.push_back(std::move(family));
        }
        std::ranges::sort(families);
        families.erase(std::ranges::unique(families).begin(), families.end());
        return families;
    }

    void set_configured_fonts(const std::u8string_view ui_family, const std::u8string_view code_family)
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.ui_family != ui_family || state.ui_typeface == nullptr)
        {
            state.ui_family = ui_family;
            state.ui_typeface = resolve_locked(state, ui_family);
            // 목록에 없는 이름은 조용히 내장 글꼴로 되돌아간다. 설정 파일은 그대로 둔다.
            if (state.ui_typeface == nullptr)
                state.ui_typeface = load_ui_typeface();
        }
        if (state.code_family != code_family || state.code_typeface == nullptr)
        {
            state.code_family = code_family;
            state.code_typeface = resolve_locked(state, code_family);
            if (state.code_typeface == nullptr)
                state.code_typeface = load_ui_typeface();
        }
    }

    sk_sp<SkTypeface> configured_ui_typeface()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.ui_typeface == nullptr)
            state.ui_typeface = load_ui_typeface();
        return state.ui_typeface;
    }

    sk_sp<SkTypeface> configured_code_typeface()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.code_typeface == nullptr)
            state.code_typeface = load_ui_typeface();
        return state.code_typeface;
    }

    sk_sp<SkTypeface> family_typeface(const std::u8string_view family)
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        return resolve_locked(state, family);
    }
} // namespace gitman::win32
