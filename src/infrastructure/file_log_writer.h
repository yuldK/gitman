#pragma once

#include "application/log_file_sink.h"
#include "application/log_file_system.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gitman {
    // 큐에 쌓아 두는 항목 수의 상한이다. 디스크가 느려도 logic이 멈추지 않도록
    // 넘치면 가장 오래된 것부터 버리고 버린 수를 파일에 남긴다 (A4.4).
    inline constexpr std::size_t log_file_queue_capacity { 8192 };

    // 카드 로그를 문서 폴더의 파일로 적재하는 구현이다 (app-shell-design A4).
    // 전용 스레드 하나가 큐를 비우며 저장소마다 세션당 파일 하나에 이어 쓴다.
    class file_log_writer final : public log_file_sink
    {
    public:
        explicit file_log_writer(log_file_system& file_system);
        ~file_log_writer() override;

        void set_document(std::u8string_view document_path, std::span<const log_file_target> targets) override;
        void append(const project_id& id, const operation_log_entry& entry) override;
        void flush() override;
        [[nodiscard]] std::optional<std::u8string> take_failure() override;

        // 상한 초과로 버린 항목 수다 (test와 진단용).
        [[nodiscard]] std::size_t dropped() const noexcept;
        // 쓰기 실패로 이 문서의 파일 로그가 꺼졌다.
        [[nodiscard]] bool disabled() const noexcept;

    private:
        // 큐에 실리는 한 건이다. document는 문서 세대이며 늦게 처리된 이전 문서의
        // 항목을 구분한다.
        struct queued_entry
        {
            std::uint64_t document { 0 };
            project_id id {};
            operation_log_entry record {};
        };

        // 저장소 하나의 파일 상태다. 파일 이름은 첫 기록 때 정한다 (A4.2).
        struct target_state
        {
            std::u8string folder {};
            std::u8string display_name {};
            std::u8string repository_path {};
            std::u8string file_path {};
        };

        // 문서 하나의 적재 상태다. 문서가 바뀌어도 이전 문서의 남은 항목을 쓸 수
        // 있도록 세대별로 들고 있다가 큐가 비면 버린다.
        struct generation_state
        {
            std::u8string root {};
            std::unordered_map<std::u8string, target_state> targets {};
            // 폴더 생성·쓰기 실패로 이 문서의 파일 로그를 껐다.
            bool disabled { false };
        };

        void writer_main();
        // 한 건을 파일에 쓴다. 실패하면 이 문서의 파일 로그를 끈다.
        void write_entry(const queued_entry& entry);
        // 현재 문서가 적재 대상이 아닌 상태다 (문서 없음 또는 실패로 꺼짐).
        [[nodiscard]] bool root_disabled() const;
        // 큐에 남지 않은 이전 문서 세대를 버린다. mutex를 쥔 채 호출한다.
        void prune_generations();

        log_file_system* file_system_ { nullptr };

        mutable std::mutex mutex_ {};
        std::condition_variable pending_ {};
        std::condition_variable drained_ {};
        std::vector<queued_entry> queue_ {};
        // set_document이 올리는 문서 세대다. 이전 문서의 큐 내용은 버린다.
        std::uint64_t document_generation_ { 0 };
        std::unordered_map<std::uint64_t, generation_state> generations_ {};
        std::size_t dropped_ { 0 };
        // 아직 파일에 적지 않은 유실 수다. 다음 기록에 한 줄로 남긴다.
        std::size_t unreported_drops_ { 0 };
        std::optional<std::u8string> failure_ {};
        bool stopping_ { false };
        bool writing_ { false };
        std::thread thread_ {};
    };
} // namespace gitman
