#include "application/task_scheduler.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace {
    // 실행 순서와 병렬성을 기록하는 executor 대역이다. 프로세스를 만들지 않는다.
    class scripted_executor final : public gitman::operation_executor
    {
    public:
        void execute(const gitman::operation_request& request, const std::function<void(gitman::logic_message)>& emit) noexcept override
        {
            try
            {
                {
                    std::unique_lock<std::mutex> lock { mutex_ };
                    started_.push_back(request.project.id.value);
                    ++running_;
                    if (running_ > peak_running_)
                        peak_running_ = running_;
                    condition_.notify_all();

                    // 병렬성 관찰용: 두 lane test에서는 다른 lane의 시작을 기다린다.
                    if (wait_for_started_ > 0)
                        condition_.wait_for(lock, std::chrono::milliseconds { 5000 }, [this] { return started_.size() >= wait_for_started_; });
                    --running_;
                }

                gitman::query_completed_event event {};
                event.operation_id = request.operation_id;
                event.generation = request.generation;
                event.id = request.project.id;
                emit(std::move(event));

                const std::lock_guard<std::mutex> lock { mutex_ };
                finished_.push_back(request.project.id.value);
                condition_.notify_all();
            }
            catch (...)
            {}
        }

        void require_parallel_start(const std::size_t count)
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            wait_for_started_ = count;
        }

        [[nodiscard]] std::vector<std::u8string> started() const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return started_;
        }

        [[nodiscard]] std::vector<std::u8string> finished() const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return finished_;
        }

        [[nodiscard]] std::size_t peak_running() const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return peak_running_;
        }

        [[nodiscard]] bool wait_finished(const std::size_t count) const
        {
            std::unique_lock<std::mutex> lock { mutex_ };
            return condition_.wait_for(lock, std::chrono::milliseconds { 5000 }, [this, count] { return finished_.size() >= count; });
        }

    private:
        mutable std::mutex mutex_ {};
        mutable std::condition_variable condition_ {};
        std::vector<std::u8string> started_ {};
        std::vector<std::u8string> finished_ {};
        std::size_t running_ { 0 };
        std::size_t peak_running_ { 0 };
        std::size_t wait_for_started_ { 0 };
    };

    gitman::operation_request make_request(const std::u8string_view id, const std::uint64_t operation_id)
    {
        gitman::operation_request request {};
        request.operation_id = operation_id;
        request.generation = 1;
        request.kind = gitman::operation_kind::query_local;
        request.project.id.value = id;
        return request;
    }

    // worker 수에 대해 서로 다른 lane으로 배정되는 id 두 개를 찾는다. hash 기반
    // 배정이라 이름은 실행마다 같고 결정적이다.
    std::pair<std::u8string, std::u8string> two_distinct_lane_ids(const std::size_t worker_count)
    {
        const std::u8string first { u8"lane-seed" };
        const std::size_t first_lane { gitman::operation_lane(gitman::project_id { first }, worker_count) };
        for (int index = 0; index < 64; ++index)
        {
            std::u8string candidate { u8"lane-candidate-" };
            const std::string digits { std::to_string(index) };
            candidate.append(digits.begin(), digits.end());
            if (gitman::operation_lane(gitman::project_id { candidate }, worker_count) != first_lane)
                return { first, candidate };
        }
        return { first, first };
    }
} // namespace

TEST_CASE("Lane assignment is deterministic and bounded", "[scheduler][app]")
{
    const gitman::project_id id { u8"alpha" };
    const std::size_t lane { gitman::operation_lane(id, 4) };
    REQUIRE(lane < 4u);
    REQUIRE(gitman::operation_lane(id, 4) == lane);
    REQUIRE(gitman::operation_lane(id, 1) == 0u);
    REQUIRE(gitman::operation_lane(gitman::project_id {}, 4) < 4u);
}

TEST_CASE("A single worker executes requests in submission order", "[scheduler][app]")
{
    scripted_executor executor {};
    messaging::channel<gitman::logic_message> logic_inbox { messaging::channel_options { 64, messaging::overflow_policy::reject_newest, {} } };
    gitman::task_scheduler scheduler { executor, logic_inbox, 1 };

    REQUIRE(scheduler.submit(make_request(u8"alpha", 1)));
    REQUIRE(scheduler.submit(make_request(u8"beta", 2)));
    REQUIRE(scheduler.submit(make_request(u8"alpha", 3)));
    REQUIRE(executor.wait_finished(3));
    scheduler.shutdown();

    const std::vector<std::u8string> started { executor.started() };
    REQUIRE(started.size() == 3u);
    REQUIRE(started[0] == u8"alpha");
    REQUIRE(started[1] == u8"beta");
    REQUIRE(started[2] == u8"alpha");
    // worker 하나면 동시 실행이 없다.
    REQUIRE(executor.peak_running() == 1u);
}

TEST_CASE("Different lanes run in parallel on separate workers", "[scheduler][app]")
{
    scripted_executor executor {};
    executor.require_parallel_start(2);
    messaging::channel<gitman::logic_message> logic_inbox { messaging::channel_options { 64, messaging::overflow_policy::reject_newest, {} } };
    gitman::task_scheduler scheduler { executor, logic_inbox, 2 };

    const auto [first, second] { two_distinct_lane_ids(2) };
    REQUIRE(gitman::operation_lane(gitman::project_id { first }, 2) != gitman::operation_lane(gitman::project_id { second }, 2));
    REQUIRE(scheduler.submit(make_request(first, 1)));
    REQUIRE(scheduler.submit(make_request(second, 2)));
    REQUIRE(executor.wait_finished(2));
    scheduler.shutdown();

    // 두 실행이 동시에 진행된 순간이 있어야 한다. 직렬이었다면 executor의 대기가
    // 5초 timeout으로 끝나 peak가 1로 남는다.
    REQUIRE(executor.peak_running() == 2u);
}

TEST_CASE("Completed operations arrive in the logic inbox", "[scheduler][app]")
{
    scripted_executor executor {};
    messaging::channel<gitman::logic_message> logic_inbox { messaging::channel_options { 64, messaging::overflow_policy::reject_newest, {} } };
    gitman::task_scheduler scheduler { executor, logic_inbox, 2 };

    REQUIRE(scheduler.submit(make_request(u8"alpha", 41)));
    REQUIRE(executor.wait_finished(1));
    scheduler.shutdown();

    messaging::envelope<gitman::logic_message> received {};
    REQUIRE(logic_inbox.try_receive(received) == messaging::receive_status::received);
    const auto* const event { std::get_if<gitman::query_completed_event>(&received.payload) };
    REQUIRE(event != nullptr);
    REQUIRE(event->operation_id == 41u);
    REQUIRE(event->id.value == u8"alpha");
}

TEST_CASE("Document loads always take lane zero", "[scheduler][app]")
{
    scripted_executor executor {};
    messaging::channel<gitman::logic_message> logic_inbox { messaging::channel_options { 64, messaging::overflow_policy::reject_newest, {} } };
    gitman::task_scheduler scheduler { executor, logic_inbox, 4 };

    gitman::operation_request load {};
    load.operation_id = 7;
    load.kind = gitman::operation_kind::load_document;
    load.document_path = u8"C:\\work\\p.version-list";
    REQUIRE(scheduler.submit(std::move(load)));
    REQUIRE(executor.wait_finished(1));
    scheduler.shutdown();
    REQUIRE(executor.started().size() == 1u);
}

TEST_CASE("Submissions after shutdown are refused and shutdown is idempotent", "[scheduler][app]")
{
    scripted_executor executor {};
    messaging::channel<gitman::logic_message> logic_inbox { messaging::channel_options { 64, messaging::overflow_policy::reject_newest, {} } };
    gitman::task_scheduler scheduler { executor, logic_inbox, 2 };

    scheduler.shutdown();
    scheduler.shutdown();
    REQUIRE_FALSE(scheduler.submit(make_request(u8"alpha", 1)));
    REQUIRE(scheduler.worker_count() == 2u);
}
