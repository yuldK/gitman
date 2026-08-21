#pragma once

#include "domain/operation_log.h"
#include "domain/project.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gitman {
    // 로그 파일을 남길 저장소 하나다 (app-shell-design A4). 폴더 이름은 sink가
    // 문서 단위로 계산하므로 여기에는 원본 값만 담는다.
    struct log_file_target
    {
        project_id id {};
        std::u8string display_name {};
        // 작업 복사본의 절대 경로다. 폴더 이름 규칙의 입력이며 머리글에도 적는다.
        std::u8string repository_path {};
    };

    // 카드 로그를 문서 폴더의 파일로 적재하는 경계다 (app-shell-design A4.4).
    // logic thread가 호출하며 절대 막히지 않아야 한다: 구현은 큐에 넣고 전용
    // 스레드가 쓴다.
    class log_file_sink
    {
    public:
        log_file_sink() = default;
        log_file_sink(const log_file_sink&) = delete;
        log_file_sink(log_file_sink&&) = delete;
        log_file_sink& operator=(const log_file_sink&) = delete;
        log_file_sink& operator=(log_file_sink&&) = delete;
        virtual ~log_file_sink() = default;

        // 활성 문서가 바뀌었다. 문서 경로가 비어 있으면(문서 닫기) 이후 항목을
        // 버린다. 이전 문서의 파일은 그대로 두고 다음 기록부터 새 파일을 만든다.
        virtual void set_document(std::u8string_view document_path, std::span<const log_file_target> targets) = 0;
        // 카드 로그에 담기는 항목과 같은 내용이다. 대상이 아닌 카드는 조용히 버린다.
        virtual void append(const project_id& id, const operation_log_entry& entry) = 0;
        // 큐가 빌 때까지 기다린다. 종료와 test가 쓴다.
        virtual void flush() = 0;
        // 파일 로그가 꺼진 사유를 한 번만 돌려준다. logic이 카드 로그에 남긴다.
        [[nodiscard]] virtual std::optional<std::u8string> take_failure() = 0;
    };
} // namespace gitman
