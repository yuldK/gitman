#include "application/function_execution_pool.h"
#include "domain/function_button.h"
#include "infrastructure/function_execution_policy.h"
#include "infrastructure/function_process_executor.h"
#include "infrastructure/operation_log_forwarder.h"
#include "platform/win32/win32_process_runner.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

// 기능 실행 풀, 기능 executor, forwarder 주기 flush test다 (function-buttons-design 4.4~4.6, 6.1 T5).
// 실제 프로세스는 `gitman_process_test_child`만 띄운다.

namespace {
    constexpr std::chrono::milliseconds wait_limit { 10000 };
    // 묶음 수를 세는 test가 쓴다. 느린 기계에서 두 레코드 사이에 주기 flush가 끼면 묶음이
    // 둘로 나뉘므로 사실상 주기 flush가 돌지 않게 한다.
    constexpr std::chrono::milliseconds no_timer_flush { std::chrono::hours { 1 } };

    std::u8string test_child_path()
    {
        std::array<wchar_t, MAX_PATH> module_path {};
        const DWORD length { GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size())) };
        REQUIRE(length > 0);

        std::filesystem::path path { std::wstring_view { module_path.data(), static_cast<std::size_t>(length) } };
        path.replace_filename(L"gitman_process_test_child.exe");
        REQUIRE(std::filesystem::exists(path));
        return path.u8string();
    }

    // 풀과 executor가 낸 event를 받은 순서와 시각으로 남긴다. 여러 worker가 함께 부른다.
    class event_recorder
    {
    public:
        struct entry
        {
            gitman::logic_message message {};
            std::chrono::steady_clock::time_point received_at {};
        };

        [[nodiscard]] std::function<void(gitman::logic_message)> sink()
        {
            return [this](gitman::logic_message message) { record(std::move(message)); };
        }

        void record(gitman::logic_message message)
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            entries_.push_back(entry { std::move(message), std::chrono::steady_clock::now() });
            condition_.notify_all();
        }

        // 다른 대역(runner)이 같은 순서 기록에 끼워 넣는 표식이다.
        void mark(std::u8string text)
        {
            gitman::function_log_event marker {};
            gitman::operation_log_entry value {};
            value.text = std::move(text);
            marker.entries.push_back(std::move(value));
            marker.run_id = marker_run_id;
            record(std::move(marker));
        }

        [[nodiscard]] bool wait_for_completed(const std::size_t count)
        {
            std::unique_lock<std::mutex> lock { mutex_ };
            return condition_.wait_for(lock, wait_limit, [this, count] { return completed_count_locked() >= count; });
        }

        [[nodiscard]] bool wait_for_started()
        {
            std::unique_lock<std::mutex> lock { mutex_ };
            return condition_.wait_for(lock, wait_limit, [this] {
                for (const entry& value : entries_)
                {
                    if (std::holds_alternative<gitman::function_started_event>(value.message))
                        return true;
                }
                return false;
            });
        }

        [[nodiscard]] std::vector<entry> snapshot()
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return entries_;
        }

        [[nodiscard]] std::vector<gitman::function_completed_event> completed()
        {
            std::vector<gitman::function_completed_event> found {};
            for (const entry& value : snapshot())
            {
                if (const auto* const event { std::get_if<gitman::function_completed_event>(&value.message) })
                    found.push_back(*event);
            }
            return found;
        }

        [[nodiscard]] std::size_t started_count()
        {
            std::size_t count { 0 };
            for (const entry& value : snapshot())
            {
                if (std::holds_alternative<gitman::function_started_event>(value.message))
                    ++count;
            }
            return count;
        }

        static constexpr std::uint64_t marker_run_id { 999999 };

    private:
        [[nodiscard]] std::size_t completed_count_locked() const
        {
            std::size_t count { 0 };
            for (const entry& value : entries_)
            {
                if (std::holds_alternative<gitman::function_completed_event>(value.message))
                    ++count;
            }
            return count;
        }

        std::mutex mutex_ {};
        std::condition_variable condition_ {};
        std::vector<entry> entries_ {};
    };

    // 풀 test용 executor 대역이다. 문이 열릴 때까지 실행을 붙잡아 실행 중인 수를 고정한다.
    class gated_executor final : public gitman::function_executor
    {
    public:
        void execute(const gitman::function_run_request& request, const std::function<void(gitman::logic_message)>& emit) noexcept override
        {
            try
            {
                {
                    std::unique_lock<std::mutex> lock { mutex_ };
                    ++running_;
                    condition_.notify_all();
                    condition_.wait_for(lock, wait_limit, [this] { return open_; });
                    --running_;
                }

                gitman::function_completed_event completed {};
                completed.id = request.id;
                completed.run_id = request.run_id;
                completed.outcome = gitman::function_run_outcome::exited;
                completed.exit_code = 0;
                emit(std::move(completed));
            }
            catch (...)
            {}
        }

        [[nodiscard]] bool wait_running(const std::size_t count)
        {
            std::unique_lock<std::mutex> lock { mutex_ };
            return condition_.wait_for(lock, wait_limit, [this, count] { return running_ >= count; });
        }

        void open()
        {
            {
                const std::lock_guard<std::mutex> lock { mutex_ };
                open_ = true;
            }
            condition_.notify_all();
        }

    private:
        std::mutex mutex_ {};
        std::condition_variable condition_ {};
        std::size_t running_ { 0 };
        bool open_ { false };
    };

    // completed를 보내지 않고 돌아오는 executor 대역이다. 계약을 어긴 executor에도 풀이
    // 자리를 돌려주는지 본다.
    class silent_executor final : public gitman::function_executor
    {
    public:
        void execute(const gitman::function_run_request& request, const std::function<void(gitman::logic_message)>& emit) noexcept override
        {
            static_cast<void>(request);
            static_cast<void>(emit);
            ++calls;
        }

        std::atomic<int> calls { 0 };
    };

    // runner 대역이다. 불린 순간을 recorder에 표식으로 남기고 정해진 출력을 sink에 흘린다.
    class scripted_runner final : public gitman::process_runner
    {
    public:
        explicit scripted_runner(event_recorder* recorder = nullptr) noexcept
            : recorder_ { recorder }
        {}

        gitman::process_result run(const gitman::process_request& request, gitman::process_output_sink* sink, const gitman::process_cancellation_token& token) noexcept override
        {
            static_cast<void>(request);
            static_cast<void>(token);
            ++calls;
            gitman::process_result result {};
            try
            {
                if (recorder_ != nullptr)
                    recorder_->mark(u8"runner called");
                for (const std::u8string& line : lines)
                {
                    gitman::process_output_record record {};
                    record.text = line;
                    if (sink != nullptr)
                        sink->on_record(record);
                }
            }
            catch (...)
            {}
            result.completion = completion;
            if (completion == gitman::process_completion::exited)
                result.exit_code = exit_code;
            return result;
        }

        std::atomic<int> calls { 0 };
        std::vector<std::u8string> lines {};
        gitman::process_completion completion { gitman::process_completion::exited };
        std::int32_t exit_code { 0 };

    private:
        event_recorder* recorder_ { nullptr };
    };

    class scripted_launcher final : public gitman::process_launcher
    {
    public:
        gitman::process_launch_result launch(const gitman::process_launch_request& request) noexcept override
        {
            static_cast<void>(request);
            ++calls;
            gitman::process_launch_result result {};
            result.status = status;
            return result;
        }

        std::atomic<int> calls { 0 };
        gitman::process_launch_status status { gitman::process_launch_status::started };
    };

    gitman::function_run_request capture_request(const std::uint64_t run_id)
    {
        gitman::function_run_request request {};
        request.id = gitman::project_id { u8"frontend" };
        request.run_id = run_id;
        request.command = gitman::process_request {};
        return request;
    }

    gitman::function_run_request detached_request(const std::uint64_t run_id)
    {
        gitman::function_run_request request {};
        request.id = gitman::project_id { u8"frontend" };
        request.run_id = run_id;
        request.command = gitman::process_launch_request {};
        return request;
    }

    // 레코드 글을 순번으로 쓴 forwarder 입력이다.
    gitman::process_output_record numbered_record(const std::size_t index)
    {
        gitman::process_output_record record {};
        record.sequence = index;
        for (const char digit : std::to_string(index))
            record.text.push_back(static_cast<char8_t>(digit));
        return record;
    }
}

TEST_CASE("The function pool rejects a fifth run instead of queueing it", "[function_buttons][t5]")
{
    gated_executor executor {};
    event_recorder recorder {};
    gitman::function_execution_pool pool { executor, recorder.sink() };
    REQUIRE(pool.capacity() == 4);

    for (std::uint64_t run_id = 1; run_id <= 4; ++run_id)
        REQUIRE(pool.submit(capture_request(run_id)));
    REQUIRE(executor.wait_running(4));
    REQUIRE(pool.in_flight() == 4);

    // 다섯 번째는 대기열에 들어가지 않고 바로 거절된다. 분리 실행도 같은 자리를 센다.
    REQUIRE_FALSE(pool.submit(capture_request(5)));
    REQUIRE_FALSE(pool.submit(detached_request(6)));
    REQUIRE(pool.in_flight() == 4);

    executor.open();
    REQUIRE(recorder.wait_for_completed(4));

    // 거절한 실행은 completed를 내지 않는다. 자리가 돌아오면 다시 받는다.
    const std::vector<gitman::function_completed_event> completed { recorder.completed() };
    REQUIRE(completed.size() == 4);
    for (const gitman::function_completed_event& event : completed)
        REQUIRE(event.run_id <= 4);
    REQUIRE(pool.submit(capture_request(7)));
    REQUIRE(recorder.wait_for_completed(5));

    pool.shutdown();
    REQUIRE_FALSE(pool.submit(capture_request(8)));
}

TEST_CASE("The function pool frees the slot before the completed event reaches logic", "[function_buttons][t5]")
{
    gated_executor executor {};
    std::vector<std::size_t> in_flight_at_completed {};
    std::mutex observed_mutex {};
    std::atomic<gitman::function_execution_pool*> observed_pool { nullptr };
    const auto sink = [&](gitman::logic_message message) {
        gitman::function_execution_pool* const pool { observed_pool.load() };
        if (pool == nullptr || std::holds_alternative<gitman::function_completed_event>(message) == false)
            return;
        const std::size_t value { pool->in_flight() };
        const std::lock_guard<std::mutex> lock { observed_mutex };
        in_flight_at_completed.push_back(value);
    };
    gitman::function_execution_pool pool { executor, sink, 1 };
    observed_pool.store(&pool);

    REQUIRE(pool.submit(capture_request(1)));
    REQUIRE(executor.wait_running(1));
    REQUIRE_FALSE(pool.submit(capture_request(2)));
    executor.open();
    pool.shutdown();

    // logic이 completed를 받고 곧바로 다음 실행을 내도 낡은 자리 수로 거절되지 않는다.
    REQUIRE(in_flight_at_completed == std::vector<std::size_t> { 0 });
}

TEST_CASE("Shutting the function pool down still completes every accepted run", "[function_buttons][t5]")
{
    // 대기열에 남은 요청이 있는 순간은 경합으로만 생기므로 제출 직후 닫기를 여러 번 되풀이한다.
    for (int round = 0; round < 50; ++round)
    {
        gated_executor executor {};
        executor.open();
        event_recorder recorder {};
        gitman::function_execution_pool pool { executor, recorder.sink() };
        for (std::uint64_t run_id = 1; run_id <= 4; ++run_id)
            REQUIRE(pool.submit(capture_request(run_id)));
        pool.shutdown();

        // 닫을 때 남은 요청도 버리지 않고 실행해 completed가 모두 나간다.
        REQUIRE(recorder.completed().size() == 4);
        REQUIRE(pool.in_flight() == 0);
        REQUIRE_FALSE(pool.submit(capture_request(5)));
    }
}

TEST_CASE("The function pool frees the slot even when the executor sends no completed event", "[function_buttons][t5]")
{
    silent_executor executor {};
    event_recorder recorder {};
    gitman::function_execution_pool pool { executor, recorder.sink(), 1 };

    REQUIRE(pool.submit(capture_request(1)));
    pool.shutdown();

    REQUIRE(executor.calls.load() == 1);
    REQUIRE(pool.in_flight() == 0);
    REQUIRE(recorder.snapshot().empty());
}

TEST_CASE("A run cancelled while waiting completes as cancelled without starting", "[function_buttons][t5]")
{
    event_recorder recorder {};
    scripted_runner runner { &recorder };
    scripted_launcher launcher {};
    gitman::function_process_executor executor { runner, launcher };
    gitman::function_execution_pool pool { executor, recorder.sink() };

    // worker가 꺼내기 전에 취소된 실행이다. 대기 시점을 경합으로 맞추지 않고 제출 전에 취소한다.
    gitman::process_cancellation_source capture_source {};
    capture_source.request_cancellation();
    gitman::function_run_request capture { capture_request(1) };
    capture.token = capture_source.token();

    gitman::process_cancellation_source detached_source {};
    detached_source.request_cancellation();
    gitman::function_run_request detached { detached_request(2) };
    detached.token = detached_source.token();

    REQUIRE(pool.submit(std::move(capture)));
    REQUIRE(pool.submit(std::move(detached)));
    REQUIRE(recorder.wait_for_completed(2));
    pool.shutdown();

    const std::vector<gitman::function_completed_event> completed { recorder.completed() };
    REQUIRE(completed.size() == 2);
    for (const gitman::function_completed_event& event : completed)
    {
        REQUIRE(event.outcome == gitman::function_run_outcome::cancelled);
        REQUIRE(event.id == gitman::project_id { u8"frontend" });
        REQUIRE_FALSE(event.exit_code.has_value());
        REQUIRE(event.duration == std::chrono::milliseconds { 0 });
    }
    REQUIRE(recorder.started_count() == 0);
    REQUIRE(runner.calls.load() == 0);
    REQUIRE(launcher.calls.load() == 0);
    REQUIRE(pool.in_flight() == 0);
}

TEST_CASE("A capture run emits started before the runner and logs before completed", "[function_buttons][t5]")
{
    event_recorder recorder {};
    scripted_runner runner { &recorder };
    runner.lines = { u8"first", u8"second" };
    runner.exit_code = 3;
    scripted_launcher launcher {};
    gitman::function_process_executor executor { runner, launcher, no_timer_flush };

    gitman::function_run_request request { capture_request(41) };
    executor.execute(request, recorder.sink());

    const std::vector<event_recorder::entry> entries { recorder.snapshot() };
    REQUIRE(entries.size() == 4);

    const auto* const started { std::get_if<gitman::function_started_event>(&entries[0].message) };
    REQUIRE(started != nullptr);
    REQUIRE(started->run_id == 41);
    REQUIRE(started->id == gitman::project_id { u8"frontend" });

    const auto* const marker { std::get_if<gitman::function_log_event>(&entries[1].message) };
    REQUIRE(marker != nullptr);
    REQUIRE(marker->run_id == event_recorder::marker_run_id);
    REQUIRE(marker->entries.front().text == u8"runner called");

    const auto* const log { std::get_if<gitman::function_log_event>(&entries[2].message) };
    REQUIRE(log != nullptr);
    REQUIRE(log->run_id == 41);
    REQUIRE(log->entries.size() == 2);
    REQUIRE(log->entries[0].text == u8"first");
    REQUIRE(log->entries[1].text == u8"second");

    const auto* const completed { std::get_if<gitman::function_completed_event>(&entries[3].message) };
    REQUIRE(completed != nullptr);
    REQUIRE(completed->run_id == 41);
    REQUIRE(completed->outcome == gitman::function_run_outcome::exited);
    REQUIRE(completed->exit_code == std::optional<std::int32_t> { 3 });
    REQUIRE(runner.calls.load() == 1);
    REQUIRE(launcher.calls.load() == 0);
}

TEST_CASE("An exception inside the executor ends the run as failed with a reason", "[function_buttons][t5]")
{
    event_recorder recorder {};
    scripted_runner runner {};
    scripted_launcher launcher {};
    gitman::function_process_executor executor { runner, launcher, no_timer_flush };

    // started를 넘기던 중 예외가 나면 runner를 부르지 않고 실패 결과로 끝난다.
    const auto sink = [&recorder](gitman::logic_message message) {
        if (std::holds_alternative<gitman::function_started_event>(message))
            throw std::runtime_error { "sink failed" };
        recorder.record(std::move(message));
    };
    executor.execute(capture_request(12), sink);

    const std::vector<gitman::function_completed_event> completed { recorder.completed() };
    REQUIRE(completed.size() == 1);
    REQUIRE(completed.front().run_id == 12);
    REQUIRE(completed.front().id == gitman::project_id { u8"frontend" });
    REQUIRE(completed.front().outcome == gitman::function_run_outcome::failed);
    REQUIRE(completed.front().diagnostics.size() == 1);
    REQUIRE(completed.front().diagnostics.front().code == gitman::diagnostic_code::operation_failed);
    REQUIRE(completed.front().diagnostics.front().severity == gitman::diagnostic_severity::error);
    REQUIRE_FALSE(completed.front().diagnostics.front().message.empty());
    REQUIRE(runner.calls.load() == 0);
}

TEST_CASE("Capture results map to function outcomes", "[function_buttons][t5]")
{
    struct expectation
    {
        gitman::process_completion completion {};
        gitman::function_run_outcome outcome {};
    };
    const std::array<expectation, 6> cases {
        expectation { gitman::process_completion::exited, gitman::function_run_outcome::exited },
        expectation { gitman::process_completion::timed_out, gitman::function_run_outcome::timed_out },
        expectation { gitman::process_completion::cancelled, gitman::function_run_outcome::cancelled },
        expectation { gitman::process_completion::start_failed, gitman::function_run_outcome::start_failed },
        expectation { gitman::process_completion::invalid_request, gitman::function_run_outcome::failed },
        expectation { gitman::process_completion::internal_error, gitman::function_run_outcome::failed },
    };

    for (const expectation& value : cases)
    {
        event_recorder recorder {};
        scripted_runner runner {};
        runner.completion = value.completion;
        runner.exit_code = 7;
        scripted_launcher launcher {};
        gitman::function_process_executor executor { runner, launcher, no_timer_flush };
        executor.execute(capture_request(1), recorder.sink());

        const std::vector<gitman::function_completed_event> completed { recorder.completed() };
        REQUIRE(completed.size() == 1);
        REQUIRE(completed.front().outcome == value.outcome);
        // 종료 코드는 끝까지 돈 실행에만 있다.
        REQUIRE(completed.front().exit_code.has_value() == (value.outcome == gitman::function_run_outcome::exited));
        REQUIRE(recorder.started_count() == 1);
    }
}

TEST_CASE("A detached run reports the launch result without a started event", "[function_buttons][t5]")
{
    for (const gitman::process_launch_status status : { gitman::process_launch_status::started, gitman::process_launch_status::start_failed, gitman::process_launch_status::invalid_request })
    {
        event_recorder recorder {};
        scripted_runner runner {};
        scripted_launcher launcher {};
        launcher.status = status;
        gitman::function_process_executor executor { runner, launcher };
        executor.execute(detached_request(9), recorder.sink());

        const std::vector<gitman::function_completed_event> completed { recorder.completed() };
        REQUIRE(completed.size() == 1);
        const gitman::function_run_outcome expected {
            status == gitman::process_launch_status::started ? gitman::function_run_outcome::launched
            : status == gitman::process_launch_status::start_failed ? gitman::function_run_outcome::start_failed
            : gitman::function_run_outcome::failed
        };
        REQUIRE(completed.front().outcome == expected);
        REQUIRE_FALSE(completed.front().exit_code.has_value());
        REQUIRE(completed.front().duration == std::chrono::milliseconds { 0 });
        REQUIRE(recorder.started_count() == 0);
        REQUIRE(launcher.calls.load() == 1);
        REQUIRE(runner.calls.load() == 0);
    }
}

TEST_CASE("Forwarder records are neither lost nor duplicated while the timer flushes", "[function_buttons][t5]")
{
    std::mutex batches_mutex {};
    std::vector<std::vector<gitman::operation_log_entry>> batches {};
    const auto collect = [&](std::vector<gitman::operation_log_entry> entries) {
        const std::lock_guard<std::mutex> lock { batches_mutex };
        batches.push_back(std::move(entries));
    };

    constexpr std::size_t total { 20000 };
    {
        // 1ms 주기로 timer flush가 reader의 on_record와 계속 겹치게 한다.
        gitman::operation_log_forwarder forwarder { collect, std::chrono::milliseconds { 1 } };
        const auto read_all = [&forwarder] {
            for (std::size_t index = 0; index < total; ++index)
            {
                forwarder.on_record(numbered_record(index));
                // 가끔 쉬어 timer가 16건이 차기 전의 묶음을 가져가게 한다.
                if (index % 500 == 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
            }
        };
        // 호출자가 따로 부르는 flush도 함께 겹친다.
        const auto flush_often = [&forwarder] {
            for (std::size_t index = 0; index < 200; ++index)
            {
                forwarder.flush();
                std::this_thread::yield();
            }
        };
        std::thread reader { read_all };
        std::thread flusher { flush_often };
        reader.join();
        flusher.join();
        forwarder.stop();
    }

    std::vector<std::u8string> received {};
    std::size_t partial_batches { 0 };
    for (const std::vector<gitman::operation_log_entry>& batch : batches)
    {
        REQUIRE_FALSE(batch.empty());
        REQUIRE(batch.size() <= gitman::operation_log_flush_count);
        if (batch.size() < gitman::operation_log_flush_count)
            ++partial_batches;
        for (const gitman::operation_log_entry& entry : batch)
            received.push_back(entry.text);
    }

    // 빠짐도 겹침도 없고 순서도 그대로다.
    REQUIRE(received.size() == total);
    for (std::size_t index = 0; index < total; ++index)
        REQUIRE(received[index] == numbered_record(index).text);
    // 16건이 차기 전에 나간 묶음이 있어야 timer·flush가 실제로 겹친 것이다.
    REQUIRE(partial_batches > 0);
}

TEST_CASE("A forwarder without a timer only flushes on count or request", "[function_buttons][t5]")
{
    std::vector<std::size_t> sizes {};
    gitman::operation_log_forwarder forwarder { [&sizes](std::vector<gitman::operation_log_entry> entries) { sizes.push_back(entries.size()); } };
    for (std::size_t index = 0; index < 20; ++index)
        forwarder.on_record(numbered_record(index));
    std::this_thread::sleep_for(std::chrono::milliseconds { 300 });
    REQUIRE(sizes == std::vector<std::size_t> { 16 });

    forwarder.stop();
    REQUIRE(sizes == std::vector<std::size_t> { 16, 4 });
}

TEST_CASE("One output line followed by two silent seconds arrives as an event within two seconds", "[function_buttons][t5][process]")
{
    event_recorder recorder {};
    const std::unique_ptr<gitman::process_runner> runner { gitman::win32::make_process_runner() };
    scripted_launcher launcher {};
    gitman::function_process_executor executor { *runner, launcher };
    gitman::function_execution_pool pool { executor, recorder.sink() };

    // 자식은 `sleeping` 한 줄을 쓰고 3초 동안 아무것도 쓰지 않은 뒤 `woke`를 쓴다.
    const std::u8string child { test_child_path() };
    gitman::function_command command {};
    command.program = child;
    command.arguments = { u8"sleep", u8"3000" };
    command.working_directory = std::filesystem::path { child }.parent_path().u8string();
    gitman::function_run_request request { capture_request(77) };
    request.command = gitman::make_function_process_request(std::move(command), 60, {});

    REQUIRE(pool.submit(std::move(request)));
    REQUIRE(recorder.wait_for_completed(1));
    pool.shutdown();

    std::optional<std::chrono::steady_clock::time_point> started_at {};
    std::optional<std::chrono::steady_clock::time_point> sleeping_at {};
    std::optional<std::chrono::steady_clock::time_point> woke_at {};
    std::optional<std::chrono::steady_clock::time_point> completed_at {};
    std::vector<std::u8string> lines {};
    for (const event_recorder::entry& value : recorder.snapshot())
    {
        if (const auto* const started { std::get_if<gitman::function_started_event>(&value.message) })
            started_at = started->started_at;
        else if (const auto* const log { std::get_if<gitman::function_log_event>(&value.message) })
        {
            REQUIRE(log->run_id == 77);
            for (const gitman::operation_log_entry& entry : log->entries)
            {
                lines.push_back(entry.text);
                if (entry.text == u8"sleeping" && sleeping_at.has_value() == false)
                    sleeping_at = value.received_at;
                if (entry.text == u8"woke" && woke_at.has_value() == false)
                    woke_at = value.received_at;
            }
        }
        else if (std::holds_alternative<gitman::function_completed_event>(value.message))
            completed_at = value.received_at;
    }

    REQUIRE(started_at.has_value());
    REQUIRE(sleeping_at.has_value());
    REQUIRE(woke_at.has_value());
    REQUIRE(completed_at.has_value());
    REQUIRE(lines == std::vector<std::u8string> { u8"sleeping", u8"woke" });
    // 16건이 차지 않아도 주기 flush가 그 한 줄을 자식이 끝나기 전에 보낸다. 2초는 줄을 쓴 때부터 잰다. started부터 재면
    // 부하 중 Debug 자식이 뜨는 데 쓴 시간까지 들어가 2.6~3.4초로 실패했다 (S5). 자식은 줄을 쓰고 3초 뒤에 `woke`를 쓰므로
    // 그 줄이 2초 안에 왔다면 `woke`보다 1초 넘게 먼저 온다.
    REQUIRE(*woke_at - *sleeping_at > std::chrono::milliseconds { 1000 });
    REQUIRE(*sleeping_at < *completed_at);

    const std::vector<gitman::function_completed_event> completed { recorder.completed() };
    REQUIRE(completed.size() == 1);
    REQUIRE(completed.front().outcome == gitman::function_run_outcome::exited);
    REQUIRE(completed.front().exit_code == std::optional<std::int32_t> { 0 });
    // 소요 시간은 started부터 잰 값이라 자식이 잔 3초를 담는다.
    REQUIRE(completed.front().duration >= std::chrono::milliseconds { 2900 });
}

TEST_CASE("A run cancelled after it started completes as cancelled after the started event", "[function_buttons][t5][process]")
{
    event_recorder recorder {};
    const std::unique_ptr<gitman::process_runner> runner { gitman::win32::make_process_runner() };
    scripted_launcher launcher {};
    gitman::function_process_executor executor { *runner, launcher };
    gitman::function_execution_pool pool { executor, recorder.sink() };

    const std::u8string child { test_child_path() };
    gitman::function_command command {};
    command.program = child;
    command.arguments = { u8"sleep", u8"3000" };
    command.working_directory = std::filesystem::path { child }.parent_path().u8string();
    gitman::process_cancellation_source source {};
    gitman::function_run_request request { capture_request(78) };
    request.command = gitman::make_function_process_request(std::move(command), 60, {});
    request.token = source.token();

    REQUIRE(pool.submit(std::move(request)));
    REQUIRE(recorder.wait_for_started());
    source.request_cancellation();
    REQUIRE(recorder.wait_for_completed(1));
    pool.shutdown();

    // started 다음에 completed{cancelled}가 오고, 자식이 잘 3초를 기다리지 않는다.
    const std::vector<event_recorder::entry> entries { recorder.snapshot() };
    REQUIRE(std::holds_alternative<gitman::function_started_event>(entries.front().message));
    REQUIRE(std::holds_alternative<gitman::function_completed_event>(entries.back().message));
    REQUIRE(recorder.started_count() == 1);

    const std::vector<gitman::function_completed_event> completed { recorder.completed() };
    REQUIRE(completed.size() == 1);
    REQUIRE(completed.front().run_id == 78);
    REQUIRE(completed.front().outcome == gitman::function_run_outcome::cancelled);
    REQUIRE_FALSE(completed.front().exit_code.has_value());
    REQUIRE(completed.front().duration < std::chrono::milliseconds { 2900 });
    REQUIRE(pool.in_flight() == 0);
}
