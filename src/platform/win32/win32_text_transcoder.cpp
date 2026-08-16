#include "platform/win32/win32_text_transcoder.h"

#include "platform/win32/utf8.h"

#include <windows.h>

#include <limits>
#include <utility>

namespace gitman::win32 {
    namespace {
        class active_code_page_transcoder final : public text_transcoder
        {
        public:
            [[nodiscard]] std::optional<std::u8string> to_utf8(const std::u8string_view bytes) noexcept override
            {
                if (bytes.empty())
                    return std::u8string {};
                if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    return std::nullopt;

                try
                {
                    return convert(bytes);
                }
                catch (...)
                {
                    return std::nullopt;
                }
            }

        private:
            [[nodiscard]] static std::optional<std::u8string> convert(const std::u8string_view bytes)
            {
                const auto* const data { reinterpret_cast<const char*>(bytes.data()) };
                const int length { static_cast<int>(bytes.size()) };
                // 활성 code page에서도 해석할 수 없는 byte가 있으면 실패로 처리해야
                // 호출자가 U+FFFD 대체로 되돌릴 수 있다.
                const int wide_length { MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, data, length, nullptr, 0) };
                if (wide_length <= 0)
                    return std::nullopt;

                std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
                if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, data, length, wide.data(), wide_length) != wide_length)
                    return std::nullopt;

                auto converted { utf16_to_utf8(wide) };
                if (converted.value.has_value() == false)
                    return std::nullopt;
                return std::move(*converted.value);
            }
        };
    } // namespace

    std::unique_ptr<text_transcoder> make_active_code_page_transcoder()
    {
        return std::make_unique<active_code_page_transcoder>();
    }
} // namespace gitman::win32
