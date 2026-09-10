#pragma once

#include "application/temp_file_writer.h"

#include <memory>

namespace gitman::win32 {
    // `%TEMP%` 아래에 gitman 전용 폴더를 만들고 그 안에 파일을 쓴다. TortoiseSVN
    // 훅의 인자 파일에 쓴다 (custom-commands-and-svn-hooks-design H4).
    //
    // 이름은 프로세스 id와 증가하는 번호로 만들어 같은 앱의 worker 여럿이 동시에
    // 써도 겹치지 않는다.
    [[nodiscard]] std::unique_ptr<temp_file_writer> make_temp_file_writer();
} // namespace gitman::win32
