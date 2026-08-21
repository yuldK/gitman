#include "application/task_scheduler.h"

#include <chrono>
#include <utility>

namespace gitman {
    namespace {
        // 문서 단위 작업(load·generate·save·discover·register)은 0번 lane에서 서로
        // 직렬화된다. register는 save와 같은 store 접근이라 반드시 직렬이어야 하고,
        // discover는 generate와 같은 탐색이라 같은 정책을 따른다.
        bool is_document_operation(const operation_kind kind) noexcept
        {
            switch (kind)
            {
            case operation_kind::load_document:
            case operation_kind::generate_document:
            case operation_kind::save_document:
            case operation_kind::discover_projects:
            case operation_kind::register_projects:
                return true;
            case operation_kind::query_local:
            case operation_kind::refresh:
            case operation_kind::update:
            case operation_kind::switch_to:
            case operation_kind::query_switch_candidates:
            case operation_kind::query_svn_directory:
            case operation_kind::query_local_changes:
            case operation_kind::query_file_diff:
                break;
            }
            return false;
        }

        // dialog가 결과를 기다리는 읽기 전용 조회다. 카드 lane에 넣으면 진행 중인
        // refresh·update 뒤에 줄을 서서 사용자가 멈춤으로 느끼므로 전용 lane에서 실행한다.
        // 저장소를 수정하지 않는 명령만 넣는다. query_switch_candidates는 git에서
        // fetch(remote ref 갱신)를 실행하므로 같은 작업 복사본의 pull과 겹치면 ref lock
        // 경합이 나서 카드 lane에 남긴다.
        bool is_interactive_query(const operation_kind kind) noexcept
        {
            switch (kind)
            {
            case operation_kind::query_svn_directory:
            case operation_kind::query_local_changes:
            case operation_kind::query_file_diff:
                return true;
            case operation_kind::load_document:
            case operation_kind::generate_document:
            case operation_kind::save_document:
            case operation_kind::discover_projects:
            case operation_kind::register_projects:
            case operation_kind::query_local:
            case operation_kind::refresh:
            case operation_kind::update:
            case operation_kind::switch_to:
            case operation_kind::query_switch_candidates:
                break;
            }
            return false;
        }
    } // namespace

    std::size_t operation_lane(const project_id& id, const std::size_t worker_count) noexcept
    {
        if (worker_count <= 1)
            return 0;

        // FNV-1a. 같은 id는 항상 같은 lane이라는 것만 중요하며 분포는 보조 목표다.
        std::uint64_t hash { 14695981039346656037ull };
        for (const char8_t value : id.value)
        {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ull;
        }
        return static_cast<std::size_t>(hash % worker_count);
    }

    task_scheduler::task_scheduler(operation_executor& executor, messaging::channel<logic_message>& logic_inbox, const std::size_t worker_count)
        : executor_ { &executor }
        , logic_inbox_ { &logic_inbox }
    {
        const std::size_t count { worker_count == 0 ? 1 : worker_count };
        // 카드 lane `count`개에 대화형 조회 전용 lane 하나를 마지막 index로 더한다.
        const std::size_t total { count + 1 };
        inboxes_.reserve(total);
        for (std::size_t index = 0; index < total; ++index)
        {
            messaging::channel_options options {};
            // scheduler가 lane당 동시 1개만 흘리는 것이 정상이라 사실상 도달하지 않는
            // 방어 상한이다 (ADR-005 설계 문서 4.1).
            options.capacity = 64;
            inboxes_.push_back(std::make_unique<messaging::channel<operation_request>>(options));
        }
        workers_.reserve(total);
        for (std::size_t index = 0; index < total; ++index)
            workers_.emplace_back(&task_scheduler::worker_loop, this, index);
    }

    task_scheduler::~task_scheduler()
    {
        shutdown();
    }

    bool task_scheduler::submit(operation_request request)
    {
        const std::size_t card_lanes { inboxes_.size() - 1 };
        std::size_t lane { operation_lane(request.project.id, card_lanes) };
        if (is_document_operation(request.kind))
            lane = 0;
        else if (is_interactive_query(request.kind))
            lane = card_lanes;
        return inboxes_[lane]->post(std::move(request)) == messaging::post_result::posted;
    }

    void task_scheduler::shutdown() noexcept
    {
        if (shut_down_)
            return;
        shut_down_ = true;

        for (const std::unique_ptr<messaging::channel<operation_request>>& inbox : inboxes_)
            inbox->close();
        for (std::thread& worker : workers_)
            if (worker.joinable())
                worker.join();
    }

    std::size_t task_scheduler::worker_count() const noexcept
    {
        // 대화형 전용 lane은 세지 않는다. 카드 분배에 쓰는 lane 수가 공약이다.
        return inboxes_.size() - 1;
    }

    void task_scheduler::worker_loop(const std::size_t index)
    {
        messaging::channel<operation_request>& inbox { *inboxes_[index] };
        messaging::envelope<operation_request> received {};
        while (true)
        {
            const messaging::receive_status status { inbox.receive_wait(received, std::chrono::milliseconds { 250 }) };
            if (status == messaging::receive_status::closed)
                return;
            if (status != messaging::receive_status::received)
                continue;

            executor_->execute(received.payload, [this](logic_message message) { emit_to_logic(std::move(message)); });
        }
    }

    void task_scheduler::emit_to_logic(logic_message message)
    {
        // event는 버리면 안 된다. inbox가 가득 차면 짧게 물러났다 다시 시도하고,
        // 닫힌 inbox(종료 중)로 가는 event만 조용히 버린다 (ADR-005 7.3).
        while (true)
        {
            logic_message attempt { message };
            const messaging::post_result result { logic_inbox_->post(std::move(attempt)) };
            if (result != messaging::post_result::channel_full)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
        }
    }
} // namespace gitman
