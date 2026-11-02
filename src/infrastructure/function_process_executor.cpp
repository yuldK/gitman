#include "infrastructure/function_process_executor.h"

#include <utility>
#include <variant>
#include <vector>

namespace gitman {
    namespace {
        function_completed_event make_completed_event(const function_run_request& request, const function_run_outcome outcome)
        {
            function_completed_event event {};
            event.id = request.id;
            event.run_id = request.run_id;
            event.outcome = outcome;
            return event;
        }

        function_run_outcome capture_outcome(const process_completion completion) noexcept
        {
            switch (completion)
            {
            case process_completion::exited:
                return function_run_outcome::exited;
            case process_completion::timed_out:
                return function_run_outcome::timed_out;
            case process_completion::cancelled:
                return function_run_outcome::cancelled;
            case process_completion::start_failed:
                return function_run_outcome::start_failed;
            case process_completion::invalid_request:
            case process_completion::internal_error:
                break;
            }
            return function_run_outcome::failed;
        }

        function_run_outcome launch_outcome(const process_launch_status status) noexcept
        {
            switch (status)
            {
            case process_launch_status::started:
                return function_run_outcome::launched;
            case process_launch_status::start_failed:
                return function_run_outcome::start_failed;
            case process_launch_status::invalid_request:
                break;
            }
            return function_run_outcome::failed;
        }
    }

    function_process_executor::function_process_executor(process_runner& runner, process_launcher& launcher, const std::chrono::milliseconds log_flush_interval) noexcept
        : runner_ { &runner }
        , launcher_ { &launcher }
        , log_flush_interval_ { log_flush_interval }
    {}

    void function_process_executor::execute(const function_run_request& request, const std::function<void(logic_message)>& emit) noexcept
    {
        try
        {
            // 대기 중에 취소됐으면 프로세스를 만들지 않는다. runner도 취소된 token이면 바로
            // 돌아오지만, completed를 여기서 보내야 started 없이 끝난다는 것이 분명하다 (4.5).
            if (request.token.cancelled())
            {
                emit(make_completed_event(request, function_run_outcome::cancelled));
                return;
            }

            if (const process_request* const capture { std::get_if<process_request>(&request.command) })
                execute_capture(request, *capture, emit);
            else
                execute_detached(request, std::get<process_launch_request>(request.command), emit);
            return;
        }
        catch (...)
        {
            // 아래에서 실패 결과를 보낸다. completed가 없으면 logic에 실행 항목이 남는다.
            // completed를 넘기던 중 예외가 났다면 같은 run id의 completed가 한 번 더 갈 수
            // 있다. logic은 이미 끝난 run id의 event를 버린다 (T6).
        }

        // run id와 결과는 예외 없이 채운다. 문자열 복사가 실패해도 logic이 run id로 찾는다.
        function_completed_event failed {};
        failed.run_id = request.run_id;
        failed.outcome = function_run_outcome::failed;
        try
        {
            failed.id = request.id;
            // `failed`는 사유를 진단으로 구별하므로 예외 경로에도 진단을 단다.
            diagnostic reason {};
            reason.code = diagnostic_code::operation_failed;
            reason.severity = diagnostic_severity::error;
            reason.message = u8"기능 실행 중 예외가 발생해 실행하지 못했습니다.";
            failed.diagnostics.push_back(std::move(reason));
        }
        catch (...)
        {
            // 진단 문자열조차 만들 수 없는 상황이므로 구조화된 결과만 보낸다.
        }

        try
        {
            emit(std::move(failed));
        }
        catch (...)
        {}
    }

    void function_process_executor::execute_capture(const function_run_request& request, const process_request& command, const std::function<void(logic_message)>& emit)
    {
        const auto forward_batch = [&request, &emit](std::vector<operation_log_entry> entries) {
            function_log_event event {};
            event.id = request.id;
            event.run_id = request.run_id;
            event.entries = std::move(entries);
            emit(std::move(event));
        };
        // timer thread를 먼저 만든다. 만들지 못하면 started 없이 실패 결과로 끝난다.
        operation_log_forwarder log { forward_batch, log_flush_interval_ };

        // started는 runner를 부르기 직전에 보낸다. 소요 시간도 여기서부터 잰다 (4.5).
        const std::chrono::steady_clock::time_point started_at { std::chrono::steady_clock::now() };
        function_started_event started {};
        started.id = request.id;
        started.run_id = request.run_id;
        started.started_at = started_at;
        emit(std::move(started));

        const process_result result { runner_->run(command, &log, request.token) };
        // 마지막 묶음이 completed보다 먼저 도착해야 한다.
        log.stop();

        function_completed_event completed { make_completed_event(request, capture_outcome(result.completion)) };
        if (completed.outcome == function_run_outcome::exited)
            completed.exit_code = result.exit_code;
        completed.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at);
        completed.output_truncated = result.output_truncated;
        completed.diagnostics = result.diagnostics;
        emit(std::move(completed));
    }

    void function_process_executor::execute_detached(const function_run_request& request, const process_launch_request& command, const std::function<void(logic_message)>& emit)
    {
        // 분리 실행은 기다리지 않으므로 started와 소요 시간이 없다 (4.1).
        process_launch_result launched { launcher_->launch(command) };
        function_completed_event completed { make_completed_event(request, launch_outcome(launched.status)) };
        completed.diagnostics = std::move(launched.diagnostics);
        emit(std::move(completed));
    }
}
