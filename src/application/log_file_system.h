#pragma once

#include <string_view>

namespace gitman {
    // 로그 파일 적재가 쓰는 최소 파일 접근이다 (app-shell-design A4.4). 문서 저장의
    // 원자적 교체와 달리 덧붙여 쓰기만 하면 되므로 계약을 따로 둔다. worker(로그
    // writer) thread에서만 호출한다.
    class log_file_system
    {
    public:
        log_file_system() = default;
        log_file_system(const log_file_system&) = delete;
        log_file_system(log_file_system&&) = delete;
        log_file_system& operator=(const log_file_system&) = delete;
        log_file_system& operator=(log_file_system&&) = delete;
        virtual ~log_file_system() = default;

        // 중간 폴더까지 만든다. 이미 있으면 성공이다.
        [[nodiscard]] virtual bool create_directories(std::u8string_view path) noexcept = 0;
        [[nodiscard]] virtual bool file_exists(std::u8string_view path) noexcept = 0;
        // 파일이 없으면 만들고 끝에 덧붙인다.
        [[nodiscard]] virtual bool append_file(std::u8string_view path, std::u8string_view bytes) noexcept = 0;
    };
} // namespace gitman
