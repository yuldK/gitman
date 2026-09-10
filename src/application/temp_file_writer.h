#pragma once

#include <string>
#include <string_view>

namespace gitman {
    // TortoiseSVN 훅은 인자를 **임시 파일**로 받는다 (경로 목록·오류 텍스트 등,
    // custom-commands-and-svn-hooks-design H4). infrastructure는 OS를 부르지 않으므로
    // 파일을 만드는 일을 계약으로 떼어 내고 구현은 Win32 adapter에 둔다.
    //
    // 붙이지 않은 조립(대부분의 test)에서는 TortoiseSVN 훅만 건너뛴다. gitman 자체
    // 훅은 인자를 환경 변수로 받아 임시 파일이 필요 없다.
    class temp_file_writer
    {
    public:
        temp_file_writer() = default;
        temp_file_writer(const temp_file_writer&) = delete;
        temp_file_writer(temp_file_writer&&) = delete;
        temp_file_writer& operator=(const temp_file_writer&) = delete;
        temp_file_writer& operator=(temp_file_writer&&) = delete;
        virtual ~temp_file_writer() = default;

        // `content`를 UTF-8 그대로 담은 파일을 만들고 절대 경로를 돌려준다. 빈
        // 내용도 파일을 만든다 — TortoiseSVN은 성공한 작업에도 빈 오류 파일을
        // 넘긴다. 실패하면 빈 값이며 예외를 던지지 않는다.
        //
        // `name_hint`는 파일 이름에 섞는 짧은 표시다 (`path`·`error` 등). 유일성은
        // 구현이 보장한다.
        [[nodiscard]] virtual std::u8string write(std::u8string_view name_hint, std::u8string_view content) noexcept = 0;

        // 더 쓰지 않는 파일을 지운다. 실패는 무시한다 — 임시 폴더의 파일이라 남아도
        // 동작에 지장이 없다.
        virtual void remove(std::u8string_view path) noexcept = 0;
    };
} // namespace gitman
