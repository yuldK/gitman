// Debug CRT의 assert·오류 보고를 표준 오류로 돌린다. 기본은 모달 대화상자라, 숨긴 창에서 도는 반복 실행이나 CI에서
// 반복자 검사 assert가 나면 출력 없이 영원히 멎는다(function-buttons-design "구현 결과" S5에서 멎은 실행). 표준 오류로
// 돌리면 보고 글이 남고 프로세스가 비정상 종료 코드로 끝난다.
#include <crtdbg.h>
#include <stdlib.h>

namespace {
    struct debug_report_mode
    {
        debug_report_mode()
        {
#ifdef _DEBUG
            static_cast<void>(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG));
            static_cast<void>(_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR));
            static_cast<void>(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG));
            static_cast<void>(_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR));
#endif
            // `abort`도 대화상자와 Windows 오류 보고 창을 띄우지 않고 끝나게 한다.
            static_cast<void>(_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
        }
    };

    const debug_report_mode report_mode {};
}
