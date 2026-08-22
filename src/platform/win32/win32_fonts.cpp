#include "platform/win32/win32_fonts.h"

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
        SkFontMgr* manager_locked(font_registry& state)
        {
            if (state.manager_ready == false)
            {
                state.manager_ready = true;
                state.manager = SkFontMgr_New_GDI();
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
            families.emplace_back(reinterpret_cast<const char8_t*>(name.c_str()), name.size());
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
