#include "infrastructure/svn_hook_runner.h"

#include "domain/path_syntax.h"
#include "infrastructure/script_command_builder.h"
#include "infrastructure/script_interpreter_discovery.h"
#include "infrastructure/vcs_command_runner.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace gitman {
    namespace {
        diagnostic make_hook_diagnostic(const diagnostic_code code, const diagnostic_severity severity, std::u8string message, const project_definition& project)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = std::move(message);
            value.source.project_id = { project.id.value };
            return value;
        }

        std::u8string_view phase_label(const svn_hook_phase phase) noexcept
        {
            switch (phase)
            {
            case svn_hook_phase::start_update:
                return u8"start_update";
            case svn_hook_phase::post_update:
                return u8"post_update";
            case svn_hook_phase::pre_update:
            default:
                return u8"pre_update";
            }
        }

        // 확장자가 있는지다. TortoiseSVN 명령줄은 `python foo.py`처럼 이름만 적을 수
        // 있어 그때는 `.exe`를 붙여 PATH에서 찾는다.
        bool has_extension(const std::u8string_view path) noexcept
        {
            std::size_t name_begin { 0 };
            for (std::size_t index = 0; index < path.size(); ++index)
                if (path[index] == u8'/' || path[index] == u8'\\')
                    name_begin = index + 1;
            const std::u8string_view name { path.substr(name_begin) };
            const std::size_t dot { name.rfind(u8'.') };
            return dot != std::u8string_view::npos && dot != 0;
        }
    } // namespace

    svn_hook_runner::svn_hook_runner(svn_hook_settings settings, std::vector<tortoise_svn_hook> tortoise_hooks, std::u8string document_path, process_runner& runner, const vcs_file_probe& probe,
        vcs_tool_environment environment, temp_file_writer* const temp_files, process_output_sink* const log) noexcept
        : settings_ { std::move(settings) }
        , tortoise_hooks_ { std::move(tortoise_hooks) }
        , document_path_ { std::move(document_path) }
        , runner_ { &runner }
        , probe_ { &probe }
        , environment_ { std::move(environment) }
        , temp_files_ { temp_files }
        , log_ { log }
    {}

    bool svn_hook_runner::empty(const std::u8string_view working_copy_path) const
    {
        if (settings_.use_tortoise_svn && temp_files_ != nullptr)
            for (const svn_hook_phase phase : { svn_hook_phase::start_update, svn_hook_phase::pre_update, svn_hook_phase::post_update })
                if (select_tortoise_svn_hook(tortoise_hooks_, phase, working_copy_path) != nullptr)
                    return false;

        for (const svn_hook& hook : settings_.hooks)
            if (svn_hook_matches(hook, working_copy_path))
                return false;
        return true;
    }

    bool svn_hook_runner::run_one(const svn_hook_context& context, const std::u8string_view label, const std::u8string_view program, std::vector<std::u8string> arguments,
        const std::int32_t timeout_seconds, const bool observe_exit_code, const process_cancellation_token& token, svn_hook_run_result& result)
    {
        // 이름만 적힌 프로그램은 PATH에서 찾는다. TortoiseSVN 명령줄이 그런 모양을
        // 허용하는데 gitman의 runner는 절대 경로만 받는다.
        std::u8string resolved { program };
        if (is_absolute_windows_path(resolved) == false)
        {
            std::u8string candidate { program };
            if (has_extension(candidate) == false)
                candidate.append(u8".exe");
            if (std::u8string found { find_executable_on_path(candidate, environment_, *probe_) }; found.empty() == false)
                resolved = std::move(found);
        }

        const resolved_script_interpreter interpreter { resolve_script_interpreter(resolved, environment_, *probe_) };
        if (interpreter.usable() == false)
        {
            for (const diagnostic& value : interpreter.diagnostics)
                result.diagnostics.push_back(value);
            result.diagnostics.push_back(make_hook_diagnostic(
                diagnostic_code::script_interpreter_unavailable, diagnostic_severity::error, std::u8string { label } + u8" 훅을 실행할 수 없습니다: " + std::u8string { program }, context.project));
            return false;
        }

        script_environment_builder environment {};
        environment.set(script_environment_project_id, context.project.id.value);
        environment.set(script_environment_project_name, context.project.display_name);
        environment.set(script_environment_repository_path, context.working_copy_path);
        environment.set(script_environment_document_path, document_path_);
        environment.set(script_environment_vcs, u8"subversion");
        environment.set(script_environment_reference, context.reference);
        environment.set(script_environment_revision, context.revision);
        environment.set(script_environment_hook_phase, phase_label(context.phase));
        environment.set(script_environment_hook_operation, context.switching ? u8"switch" : u8"update");
        environment.set(script_environment_hook_target, context.target_url);
        environment.set(script_environment_hook_error, context.error_text);

        script_command command {};
        command.script = resolved;
        command.arguments = std::move(arguments);
        command.working_directory = context.working_copy_path;
        command.environment = environment.values();
        command.timeout_seconds = timeout_seconds;

        const process_request process { make_script_process_request(interpreter, command) };
        if (std::vector<diagnostic> invalid { validate_process_request(process) }; invalid.empty() == false)
        {
            for (diagnostic& value : invalid)
                result.diagnostics.push_back(std::move(value));
            return false;
        }

        if (log_ != nullptr)
        {
            // 무엇이 실행되는지 남긴다. 훅은 사용자가 누르지 않아도 도는 코드라
            // 명령보다 더 중요한 기록이다 (설계 0장).
            process_output_record record {};
            record.stream = process_stream::standard_output;
            record.text = std::u8string { label } + u8" 훅 실행: " + describe_process_command(process);
            log_->on_record(record);
        }

        const vcs_command_result run_result { run_vcs_command(*runner_, process, token, log_) };
        ++result.executed;

        // 기다리지 않는 TortoiseSVN 훅은 그쪽에서도 종료 코드를 보지 않는다. 프로세스는
        // 그대로 기다리되 판정에 쓰지 않아 결과를 같게 만든다 (H5).
        if (observe_exit_code == false)
            return true;
        if (run_result.succeeded())
            return true;

        std::u8string message { label };
        message.append(u8" 훅이 실패했습니다");
        if (run_result.process.exit_code.has_value())
        {
            const std::string digits { std::to_string(*run_result.process.exit_code) };
            message.append(u8" (종료 코드 ");
            message.append(digits.begin(), digits.end());
            message.append(u8")");
        }
        message.append(u8".");
        result.diagnostics.push_back(make_hook_diagnostic(diagnostic_code::script_command_failed, diagnostic_severity::error, std::move(message), context.project));
        return false;
    }

    bool svn_hook_runner::run_tortoise_hook(const svn_hook_context& context, const process_cancellation_token& token, svn_hook_run_result& result)
    {
        if (settings_.use_tortoise_svn == false || temp_files_ == nullptr)
            return true;

        const tortoise_svn_hook* const hook { select_tortoise_svn_hook(tortoise_hooks_, context.phase, context.working_copy_path) };
        if (hook == nullptr)
            return true;

        const tortoise_command_line command_line { split_tortoise_command_line(hook->command_line) };
        if (command_line.program.empty())
            return true;

        // TortoiseSVN은 인자를 임시 파일로 넘긴다 (H4). PATH 파일에는 작업 복사본
        // 루트 한 줄이 들어가고, ERROR는 성공해도 빈 파일로 만든다.
        std::vector<std::u8string> created {};
        const auto make_file = [this, &created](const std::u8string_view hint, const std::u8string_view content) {
            std::u8string path { temp_files_->write(hint, content) };
            if (path.empty() == false)
                created.push_back(path);
            return path;
        };

        std::vector<std::u8string> arguments { command_line.arguments };
        const std::u8string path_file { make_file(u8"path", context.working_copy_path + std::u8string { u8"\r\n" }) };
        arguments.push_back(path_file);

        if (context.phase != svn_hook_phase::start_update)
        {
            // gitman은 `svn update`에 `--depth`를 주지 않으므로 언제나 infinity(3)다.
            arguments.push_back(std::u8string { u8"3" });
            // 리비전을 지정해 update하지 않는다.
            arguments.push_back(std::u8string { u8"HEAD" });
        }
        if (context.phase == svn_hook_phase::post_update)
            arguments.push_back(make_file(u8"error", context.error_text));

        arguments.push_back(context.working_copy_path);

        if (context.phase == svn_hook_phase::post_update)
        {
            // 실제로 건드린 경로 목록이다. gitman은 `svn update` 출력을 경로 목록으로
            // 파싱하지 않으므로 빈 파일을 넘긴다 (알려진 차이, H4).
            arguments.push_back(make_file(u8"result", {}));
        }

        const bool succeeded { run_one(context, phase_label(context.phase), command_line.program, std::move(arguments), 0, hook->wait, token, result) };
        for (const std::u8string& path : created)
            temp_files_->remove(path);
        return succeeded;
    }

    svn_hook_run_result svn_hook_runner::run(const svn_hook_context& context, const process_cancellation_token& token)
    {
        svn_hook_run_result result {};
        const bool blocking_phase { context.phase != svn_hook_phase::post_update };

        if (token.cancelled())
            return result;

        // 1. TortoiseSVN 훅이 먼저다. 사용자가 이미 그 도구로 세워 둔 규칙이라
        //    gitman의 훅보다 앞선다 (H2).
        if (run_tortoise_hook(context, token, result) == false)
        {
            result.blocked = blocking_phase;
            return result;
        }

        // 2. 문서·전역이 정의한 gitman 훅을 목록 순서대로 실행한다.
        for (const svn_hook& hook : settings_.hooks)
        {
            if (hook.phase != context.phase || svn_hook_matches(hook, context.working_copy_path) == false)
                continue;
            if (token.cancelled())
                return result;

            const std::u8string script { resolve_script_path(hook.script, document_path_) };
            if (run_one(context, phase_label(context.phase), script, hook.arguments, hook.timeout_seconds, true, token, result) == false)
            {
                result.blocked = blocking_phase;
                return result;
            }
        }
        return result;
    }
} // namespace gitman
