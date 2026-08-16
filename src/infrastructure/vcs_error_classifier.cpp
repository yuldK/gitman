#include "infrastructure/vcs_error_classifier.h"

#include <array>
#include <cstddef>
#include <span>

namespace gitman {
    namespace {
        char8_t ascii_lowercase(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        bool is_ascii_digit(const char8_t value) noexcept
        {
            return value >= u8'0' && value <= u8'9';
        }

        bool contains_ignoring_case(const std::u8string_view text, const std::u8string_view needle) noexcept
        {
            if (needle.empty() || text.size() < needle.size())
                return false;
            const std::size_t last { text.size() - needle.size() };
            for (std::size_t begin = 0; begin <= last; ++begin)
            {
                std::size_t index { 0 };
                while (index < needle.size() && ascii_lowercase(text[begin + index]) == ascii_lowercase(needle[index]))
                    ++index;
                if (index == needle.size())
                    return true;
            }
            return false;
        }

        bool contains_any(const std::u8string_view text, const std::span<const std::u8string_view> needles) noexcept
        {
            for (const std::u8string_view needle : needles)
                if (contains_ignoring_case(text, needle))
                    return true;
            return false;
        }

        // 번역된 문장 안에서도 숫자 인자는 그대로 남는다. 다만 branch 이름이나 경로에
        // 우연히 같은 숫자가 들어갈 수 있으므로 앞뒤가 숫자가 아닌 독립 토큰만 세고,
        // HTTP 전송 문맥일 때만 인정한다.
        bool contains_http_status(const std::u8string_view text, const std::u8string_view status) noexcept
        {
            if (contains_ignoring_case(text, u8"http") == false)
                return false;

            std::size_t begin { text.find(status) };
            while (begin != std::u8string_view::npos)
            {
                const bool digit_before { begin > 0 && is_ascii_digit(text[begin - 1]) };
                const std::size_t after { begin + status.size() };
                const bool digit_after { after < text.size() && is_ascii_digit(text[after]) };
                if (digit_before == false && digit_after == false)
                    return true;
                begin = text.find(status, begin + 1);
            }
            return false;
        }

        vcs_failure_kind classify_git_error(const std::u8string_view text) noexcept
        {
            // OpenSSH가 만드는 문자열이라 번역되지 않는다.
            constexpr std::array ssh_authentication {
                std::u8string_view { u8"Permission denied (publickey" },
                std::u8string_view { u8"Permission denied, please try again" },
                std::u8string_view { u8"Host key verification failed" },
                std::u8string_view { u8"no matching host key type" },
            };
            if (contains_any(text, ssh_authentication))
                return vcs_failure_kind::authentication_required;
            if (contains_http_status(text, u8"401") || contains_http_status(text, u8"403"))
                return vcs_failure_kind::authentication_required;

            // libcurl이 만드는 문자열이며 libcurl에는 번역 catalog가 없다.
            constexpr std::array curl_network {
                std::u8string_view { u8"Could not resolve host" },
                std::u8string_view { u8"Could not resolve proxy" },
                std::u8string_view { u8"Failed to connect" },
                std::u8string_view { u8"Connection timed out" },
                std::u8string_view { u8"Connection refused" },
                std::u8string_view { u8"Operation timed out" },
                std::u8string_view { u8"Empty reply from server" },
                std::u8string_view { u8"Connection reset by peer" },
            };
            if (contains_any(text, curl_network))
                return vcs_failure_kind::offline;

            // 서버가 그대로 돌려주는 문장이라 Git의 번역 대상이 아니다.
            if (contains_ignoring_case(text, u8"Repository not found") || contains_http_status(text, u8"404"))
                return vcs_failure_kind::repository_not_found;

            // 아래는 번역 대상이라 1차 근거로 쓰지 않는다. 위 신호가 모두 없을 때만
            // 영어 환경을 위한 보조 신호로 확인한다.
            constexpr std::array translated_authentication {
                std::u8string_view { u8"could not read Username" },
                std::u8string_view { u8"could not read Password" },
                std::u8string_view { u8"terminal prompts disabled" },
                std::u8string_view { u8"Authentication failed" },
            };
            if (contains_any(text, translated_authentication))
                return vcs_failure_kind::authentication_required;
            if (contains_ignoring_case(text, u8"not a git repository"))
                return vcs_failure_kind::repository_not_found;
            return vcs_failure_kind::command_failed;
        }

        vcs_failure_kind classify_subversion_error(const std::u8string_view text) noexcept
        {
            // SVN은 번역된 메시지에도 오류 코드를 그대로 붙인다. 코드가 가장 구체적인
            // 신호이므로 인증, 저장소, 네트워크 순으로 확인한다. 연결 실패(E170013)는
            // 더 구체적인 인증 코드와 함께 나오는 경우가 많아 뒤에 둔다.
            constexpr std::array authentication_codes {
                std::u8string_view { u8"E170001" },
                std::u8string_view { u8"E215004" },
            };
            if (contains_any(text, authentication_codes))
                return vcs_failure_kind::authentication_required;

            constexpr std::array repository_codes {
                std::u8string_view { u8"E155007" },
                std::u8string_view { u8"E155010" },
                std::u8string_view { u8"E160013" },
                std::u8string_view { u8"E200009" },
            };
            if (contains_any(text, repository_codes))
                return vcs_failure_kind::repository_not_found;

            constexpr std::array network_codes {
                std::u8string_view { u8"E170013" },
                std::u8string_view { u8"E175002" },
                std::u8string_view { u8"E730054" },
                std::u8string_view { u8"E670002" },
                std::u8string_view { u8"E670003" },
            };
            if (contains_any(text, network_codes))
                return vcs_failure_kind::offline;

            constexpr std::array translated_authentication {
                std::u8string_view { u8"Authentication failed" },
                std::u8string_view { u8"Authorization failed" },
                std::u8string_view { u8"No more credentials" },
            };
            if (contains_any(text, translated_authentication))
                return vcs_failure_kind::authentication_required;
            if (contains_ignoring_case(text, u8"is not a working copy"))
                return vcs_failure_kind::repository_not_found;
            return vcs_failure_kind::command_failed;
        }
    } // namespace

    vcs_failure_kind classify_vcs_failure(const repository_kind kind, const vcs_command_result& result) noexcept
    {
        switch (result.process.completion)
        {
        case process_completion::timed_out:
            return vcs_failure_kind::timed_out;
        case process_completion::cancelled:
            return vcs_failure_kind::cancelled;
        case process_completion::start_failed:
        case process_completion::invalid_request:
        case process_completion::internal_error:
            return vcs_failure_kind::execution_failed;
        case process_completion::exited:
            break;
        }

        if (result.succeeded())
            return vcs_failure_kind::none;

        try
        {
            return classify_vcs_error_text(kind, result.standard_error_text());
        }
        catch (...)
        {
            return vcs_failure_kind::command_failed;
        }
    }

    vcs_failure_kind classify_vcs_error_text(const repository_kind kind, const std::u8string_view standard_error) noexcept
    {
        return kind == repository_kind::subversion ? classify_subversion_error(standard_error) : classify_git_error(standard_error);
    }

    remote_sync_state remote_sync_state_for_failure(const vcs_failure_kind failure) noexcept
    {
        switch (failure)
        {
        case vcs_failure_kind::none:
            return remote_sync_state::unknown;
        case vcs_failure_kind::authentication_required:
            return remote_sync_state::authentication_required;
        case vcs_failure_kind::offline:
        case vcs_failure_kind::timed_out:
            return remote_sync_state::offline;
        case vcs_failure_kind::cancelled:
            return remote_sync_state::unknown;
        case vcs_failure_kind::repository_not_found:
        case vcs_failure_kind::execution_failed:
        case vcs_failure_kind::command_failed:
            return remote_sync_state::error;
        }
        return remote_sync_state::error;
    }

    diagnostic_code diagnostic_code_for_failure(const vcs_failure_kind failure) noexcept
    {
        switch (failure)
        {
        case vcs_failure_kind::none:
            return diagnostic_code::unknown;
        case vcs_failure_kind::authentication_required:
            return diagnostic_code::authentication_required;
        case vcs_failure_kind::offline:
            return diagnostic_code::remote_unreachable;
        case vcs_failure_kind::repository_not_found:
            return diagnostic_code::repository_not_found;
        case vcs_failure_kind::timed_out:
            return diagnostic_code::process_timed_out;
        case vcs_failure_kind::cancelled:
            return diagnostic_code::process_cancelled;
        case vcs_failure_kind::execution_failed:
            return diagnostic_code::process_start_failed;
        case vcs_failure_kind::command_failed:
            return diagnostic_code::vcs_command_failed;
        }
        return diagnostic_code::vcs_command_failed;
    }

    std::u8string_view vcs_failure_kind_name(const vcs_failure_kind failure) noexcept
    {
        switch (failure)
        {
        case vcs_failure_kind::none:
            return u8"none";
        case vcs_failure_kind::authentication_required:
            return u8"authentication_required";
        case vcs_failure_kind::offline:
            return u8"offline";
        case vcs_failure_kind::repository_not_found:
            return u8"repository_not_found";
        case vcs_failure_kind::timed_out:
            return u8"timed_out";
        case vcs_failure_kind::cancelled:
            return u8"cancelled";
        case vcs_failure_kind::execution_failed:
            return u8"execution_failed";
        case vcs_failure_kind::command_failed:
            return u8"command_failed";
        }
        return u8"command_failed";
    }

    std::u8string_view vcs_failure_message(const vcs_failure_kind failure) noexcept
    {
        switch (failure)
        {
        case vcs_failure_kind::none:
            return u8"명령이 성공했습니다.";
        case vcs_failure_kind::authentication_required:
            return u8"인증이 필요하거나 자격 증명이 거부되었습니다.";
        case vcs_failure_kind::offline:
            return u8"원격 저장소에 연결하지 못했습니다.";
        case vcs_failure_kind::repository_not_found:
            return u8"저장소를 찾을 수 없습니다.";
        case vcs_failure_kind::timed_out:
            return u8"명령이 제한 시간을 넘겨 중단되었습니다.";
        case vcs_failure_kind::cancelled:
            return u8"명령이 취소되었습니다.";
        case vcs_failure_kind::execution_failed:
            return u8"명령을 실행하지 못했습니다.";
        case vcs_failure_kind::command_failed:
            return u8"명령이 실패했습니다.";
        }
        return u8"명령이 실패했습니다.";
    }
} // namespace gitman
