#include "platform/win32/win32_process_runner.h"

#include "infrastructure/command_line_builder.h"
#include "infrastructure/process_output_pipeline.h"
#include "platform/win32/utf8.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gitman::win32 {
    namespace {
        class unique_handle
        {
        public:
            unique_handle() noexcept = default;

            explicit unique_handle(const HANDLE value) noexcept
                : value_ { value }
            {}

            unique_handle(const unique_handle&) = delete;
            unique_handle& operator=(const unique_handle&) = delete;

            unique_handle(unique_handle&& other) noexcept
                : value_ { other.release() }
            {}

            unique_handle& operator=(unique_handle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    value_ = other.release();
                }
                return *this;
            }

            ~unique_handle()
            {
                reset();
            }

            [[nodiscard]] HANDLE get() const noexcept
            {
                return value_;
            }

            [[nodiscard]] bool valid() const noexcept
            {
                return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
            }

            [[nodiscard]] HANDLE release() noexcept
            {
                const HANDLE value { value_ };
                value_ = INVALID_HANDLE_VALUE;
                return value;
            }

            void reset(const HANDLE value = INVALID_HANDLE_VALUE) noexcept
            {
                if (valid())
                    CloseHandle(value_);
                value_ = value;
            }

        private:
            HANDLE value_ { INVALID_HANDLE_VALUE };
        };

        // 상속할 handle을 명시하지 않으면 부모의 다른 상속 가능 handle까지 자식에게
        // 넘어간다. 그래서 attribute list로 stdin, stdout, stderr만 상속시킨다.
        class unique_attribute_list
        {
        public:
            unique_attribute_list() noexcept = default;
            unique_attribute_list(const unique_attribute_list&) = delete;
            unique_attribute_list(unique_attribute_list&&) = delete;
            unique_attribute_list& operator=(const unique_attribute_list&) = delete;
            unique_attribute_list& operator=(unique_attribute_list&&) = delete;

            ~unique_attribute_list()
            {
                if (initialized_)
                    DeleteProcThreadAttributeList(get());
            }

            [[nodiscard]] bool initialize(const DWORD count)
            {
                SIZE_T required { 0 };
                InitializeProcThreadAttributeList(nullptr, count, 0, &required);
                if (required == 0)
                    return false;

                storage_.assign(static_cast<std::size_t>(required), std::byte {});
                if (InitializeProcThreadAttributeList(get(), count, 0, &required) == FALSE)
                    return false;
                initialized_ = true;
                return true;
            }

            [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() noexcept
            {
                return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
            }

        private:
            std::vector<std::byte> storage_ {};
            bool initialized_ { false };
        };

        class environment_strings
        {
        public:
            environment_strings() noexcept
                : value_ { GetEnvironmentStringsW() }
            {}

            environment_strings(const environment_strings&) = delete;
            environment_strings(environment_strings&&) = delete;
            environment_strings& operator=(const environment_strings&) = delete;
            environment_strings& operator=(environment_strings&&) = delete;

            ~environment_strings()
            {
                if (value_ != nullptr)
                    FreeEnvironmentStringsW(value_);
            }

            [[nodiscard]] const wchar_t* get() const noexcept
            {
                return value_;
            }

        private:
            wchar_t* value_ { nullptr };
        };

        struct environment_entry
        {
            std::wstring name {};
            std::wstring value {};
        };

        struct environment_block
        {
            // 값이 없고 실패도 아니면 부모 환경을 그대로 상속한다.
            std::optional<std::wstring> block {};
            std::uint32_t native_error { ERROR_SUCCESS };
            bool failed { false };
        };

        std::uint32_t last_error_or(const std::uint32_t fallback) noexcept
        {
            const DWORD error { GetLastError() };
            return error == ERROR_SUCCESS ? fallback : static_cast<std::uint32_t>(error);
        }

        diagnostic make_process_diagnostic(
            const diagnostic_code code, std::u8string message, const std::optional<std::uint32_t> native_error = std::nullopt, const diagnostic_severity severity = diagnostic_severity::error)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = std::move(message);
            value.native_error = native_error;
            return value;
        }

        void terminate_child(const HANDLE process) noexcept
        {
            // 콘솔 없는 자식에게 보낼 안전한 graceful signal이 없으므로 즉시 종료한다.
            TerminateProcess(process, static_cast<UINT>(ERROR_PROCESS_ABORTED));
        }

        void normalize_separators(std::wstring& path) noexcept
        {
            for (wchar_t& value : path)
                if (value == L'/')
                    value = L'\\';
        }

        int compare_environment_names(const std::wstring_view left, const std::wstring_view right) noexcept
        {
            if (left.empty() || right.empty())
                return left.empty() && right.empty() ? 0 : (left.empty() ? -1 : 1);

            const int result { CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) };
            // API가 실패하면 대소문자를 구분하는 비교로 대체한다. 순서만 안정적이면 된다.
            if (result == 0)
                return left.compare(right);
            return result - CSTR_EQUAL;
        }

        std::vector<environment_entry> parent_environment_entries()
        {
            std::vector<environment_entry> entries {};
            const environment_strings strings {};
            if (strings.get() == nullptr)
                return entries;

            for (const wchar_t* cursor = strings.get(); *cursor != L'\0';)
            {
                const std::wstring_view text { cursor };
                cursor += text.size() + 1;
                // `=C:` 형태의 drive 현재 디렉터리 항목은 이름이 `=`로 시작하므로 1부터 찾는다.
                const std::size_t separator { text.find(L'=', 1) };
                if (separator == std::wstring_view::npos)
                    continue;
                entries.push_back(environment_entry { std::wstring { text.substr(0, separator) }, std::wstring { text.substr(separator + 1) } });
            }
            return entries;
        }

        environment_block make_environment_block(const std::vector<process_environment_override>& overrides)
        {
            if (overrides.empty())
                return {};

            std::vector<environment_entry> entries { parent_environment_entries() };
            for (const process_environment_override& entry : overrides)
            {
                const auto name { utf8_to_utf16(entry.name) };
                if (name.value.has_value() == false)
                    return { std::nullopt, static_cast<std::uint32_t>(name.error->native_error), true };

                const auto existing { std::ranges::find_if(entries, [&name](const environment_entry& value) { return compare_environment_names(value.name, *name.value) == 0; }) };
                if (entry.value.has_value() == false)
                {
                    if (existing != entries.end())
                        entries.erase(existing);
                    continue;
                }

                const auto value { utf8_to_utf16(*entry.value) };
                if (value.value.has_value() == false)
                    return { std::nullopt, static_cast<std::uint32_t>(value.error->native_error), true };
                if (existing != entries.end())
                    existing->value = *value.value;
                else
                    entries.push_back(environment_entry { *name.value, *value.value });
            }

            // CreateProcess에 넘기는 block은 이름 기준 대소문자 무시 정렬을 요구한다.
            std::ranges::sort(entries, [](const environment_entry& left, const environment_entry& right) { return compare_environment_names(left.name, right.name) < 0; });

            std::wstring block {};
            for (const environment_entry& entry : entries)
            {
                block.append(entry.name);
                block.push_back(L'=');
                block.append(entry.value);
                block.push_back(L'\0');
            }
            // std::wstring이 마지막에 암시적 null을 하나 더 두므로 block은 이중 null로 끝난다.
            return { std::move(block), ERROR_SUCCESS, false };
        }

        unique_handle open_null_device(const DWORD access)
        {
            SECURITY_ATTRIBUTES inheritable {};
            inheritable.nLength = sizeof(inheritable);
            inheritable.lpSecurityDescriptor = nullptr;
            inheritable.bInheritHandle = TRUE;

            SetLastError(ERROR_SUCCESS);
            return unique_handle { CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr) };
        }

        struct output_channel
        {
            unique_handle read {};
            unique_handle write {};
        };

        // 자식에게 넘길 쓰기 end만 상속 가능하게 만든다. 읽기 end가 자식에게 남으면
        // 자식이 살아 있는 동안 부모가 EOF를 볼 수 없다.
        bool create_output_channel(output_channel& channel) noexcept
        {
            SECURITY_ATTRIBUTES inheritable {};
            inheritable.nLength = sizeof(inheritable);
            inheritable.lpSecurityDescriptor = nullptr;
            inheritable.bInheritHandle = TRUE;

            HANDLE read_end { INVALID_HANDLE_VALUE };
            HANDLE write_end { INVALID_HANDLE_VALUE };
            SetLastError(ERROR_SUCCESS);
            if (CreatePipe(&read_end, &write_end, &inheritable, 0) == FALSE)
                return false;

            channel.read.reset(read_end);
            channel.write.reset(write_end);
            return SetHandleInformation(channel.read.get(), HANDLE_FLAG_INHERIT, 0) != FALSE;
        }

        // 레코드에 실행 단위 sequence를 부여하고 sink 호출을 직렬화한다. 두 reader
        // 스레드가 동시에 도착해도 한 실행 안의 순서는 고정된다.
        class output_collector
        {
        public:
            explicit output_collector(process_output_sink* const sink) noexcept
                : sink_ { sink }
            {}

            output_collector(const output_collector&) = delete;
            output_collector(output_collector&&) = delete;
            output_collector& operator=(const output_collector&) = delete;
            output_collector& operator=(output_collector&&) = delete;
            ~output_collector() = default;

            void submit(process_output_record& record) noexcept
            {
                const std::lock_guard<std::mutex> guard { mutex_ };
                record.sequence = next_sequence_++;
                ++count_;
                if (sink_ == nullptr)
                    return;

                try
                {
                    sink_->on_record(record);
                }
                catch (...)
                {
                    // sink 예외가 reader 스레드를 종료시키면 자식이 pipe에서 막힌다.
                    sink_failed_ = true;
                }
            }

            [[nodiscard]] std::uint64_t count() const noexcept
            {
                const std::lock_guard<std::mutex> guard { mutex_ };
                return count_;
            }

            [[nodiscard]] bool sink_failed() const noexcept
            {
                const std::lock_guard<std::mutex> guard { mutex_ };
                return sink_failed_;
            }

        private:
            mutable std::mutex mutex_ {};
            std::uint64_t next_sequence_ { 1 };
            std::uint64_t count_ { 0 };
            bool sink_failed_ { false };
            process_output_sink* sink_ { nullptr };
        };

        void read_output_stream(const HANDLE pipe, process_output_pipeline& pipeline, output_collector& collector) noexcept
        {
            const process_output_pipeline::record_handler handler { [&collector](process_output_record& record) { collector.submit(record); } };
            try
            {
                std::u8string buffer(process_read_block_byte_size, u8'\0');
                while (true)
                {
                    DWORD read_count { 0 };
                    // 자식이 끝나고 쓰기 end가 모두 닫히면 ERROR_BROKEN_PIPE로 반환된다.
                    if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read_count, nullptr) == FALSE)
                        break;
                    if (read_count == 0)
                        break;
                    pipeline.append(std::u8string_view { buffer.data(), static_cast<std::size_t>(read_count) }, handler);
                }
                pipeline.flush(handler);
            }
            catch (...)
            {
                // 수집 실패는 실행 결과의 절단 표시로만 남기고 스레드를 정상 종료한다.
            }
        }

        void finish_result(process_result& result, const std::chrono::steady_clock::time_point started)
        {
            result.finished_at = std::chrono::system_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        }

        process_result make_unexpected_failure_result() noexcept
        {
            process_result result {};
            result.completion = process_completion::start_failed;
            result.native_error = ERROR_NOT_ENOUGH_MEMORY;
            try
            {
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::process_start_failed, u8"프로세스 실행 중 예기치 않은 오류가 발생했습니다.", result.native_error));
            }
            catch (...)
            {
                // 진단 문자열조차 만들 수 없는 상황이므로 구조화된 결과만 반환한다.
            }
            return result;
        }

        process_result make_reader_failure_result(process_result result, const std::chrono::steady_clock::time_point started)
        {
            result.completion = process_completion::internal_error;
            result.native_error = ERROR_NOT_ENOUGH_MEMORY;
            result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::process_pipe_failed, u8"출력 수집 스레드를 만들지 못해 자식 프로세스를 종료했습니다.", result.native_error));
            finish_result(result, started);
            return result;
        }

        process_result run_impl(const process_request& request, process_output_sink* const sink)
        {
            process_result result {};
            std::vector<diagnostic> validation { validate_process_request(request) };
            if (validation.empty() == false)
            {
                result.completion = process_completion::invalid_request;
                result.diagnostics = std::move(validation);
                return result;
            }

            // 실제 실행에 쓰는 명령줄과 기록용 값을 분리해 둔다. `S3-D5-CODE`가 기록용
            // 값에만 마스킹을 적용해도 실행 인자가 바뀌지 않는다.
            const std::u8string command_line_text { build_windows_command_line(request.executable, request.arguments) };
            result.masked_command_line = command_line_text;

            const auto executable { utf8_to_utf16(request.executable) };
            const auto command_line { utf8_to_utf16(command_line_text) };
            const auto working_directory { utf8_to_utf16(request.working_directory) };
            if (executable.value.has_value() == false || command_line.value.has_value() == false || working_directory.value.has_value() == false)
            {
                result.completion = process_completion::start_failed;
                result.native_error = ERROR_NO_UNICODE_TRANSLATION;
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::process_start_failed, u8"실행 요청의 UTF-8 값을 Windows 문자열로 변환하지 못했습니다.", result.native_error));
                return result;
            }

            const environment_block environment { make_environment_block(request.environment_overrides) };
            if (environment.failed)
            {
                result.completion = process_completion::start_failed;
                result.native_error = environment.native_error;
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::process_start_failed, u8"환경 변수 override를 Windows 문자열로 변환하지 못했습니다.", result.native_error));
                return result;
            }

            // 자식이 프롬프트를 읽어도 즉시 EOF가 되도록 stdin은 항상 NUL이다.
            unique_handle input { open_null_device(GENERIC_READ) };
            output_channel standard_output {};
            output_channel standard_error {};
            if (input.valid() == false || create_output_channel(standard_output) == false || create_output_channel(standard_error) == false)
            {
                result.completion = process_completion::start_failed;
                result.native_error = last_error_or(ERROR_INVALID_HANDLE);
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::process_pipe_failed, u8"자식 프로세스의 표준 handle을 준비하지 못했습니다.", result.native_error));
                return result;
            }

            unique_attribute_list attributes {};
            HANDLE inherited[] { input.get(), standard_output.write.get(), standard_error.write.get() };
            STARTUPINFOEXW startup {};
            startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput = input.get();
            startup.StartupInfo.hStdOutput = standard_output.write.get();
            startup.StartupInfo.hStdError = standard_error.write.get();

            SetLastError(ERROR_SUCCESS);
            if (attributes.initialize(1) == false || UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) == FALSE)
            {
                result.completion = process_completion::start_failed;
                result.native_error = last_error_or(ERROR_INVALID_PARAMETER);
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::process_start_failed, u8"상속 handle 목록을 준비하지 못했습니다.", result.native_error));
                return result;
            }
            startup.lpAttributeList = attributes.get();

            std::wstring application_path { *executable.value };
            std::wstring current_directory { *working_directory.value };
            normalize_separators(application_path);
            normalize_separators(current_directory);
            // `CreateProcessW`는 명령줄 buffer를 수정할 수 있으므로 쓰기 가능한 사본을 넘긴다.
            std::wstring mutable_command_line { *command_line.value };

            void* environment_pointer { nullptr };
            if (environment.block.has_value())
                environment_pointer = const_cast<wchar_t*>(environment.block->c_str());

            PROCESS_INFORMATION created {};
            const std::chrono::steady_clock::time_point started { std::chrono::steady_clock::now() };
            result.started_at = std::chrono::system_clock::now();
            SetLastError(ERROR_SUCCESS);
            const BOOL launched {
                CreateProcessW(application_path.c_str(), mutable_command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                    environment_pointer, current_directory.c_str(), &startup.StartupInfo, &created),
            };
            if (launched == FALSE)
            {
                result.completion = process_completion::start_failed;
                result.native_error = last_error_or(ERROR_FILE_NOT_FOUND);
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::process_start_failed, u8"자식 프로세스를 시작하지 못했습니다.", result.native_error));
                finish_result(result, started);
                return result;
            }

            const unique_handle process_handle { created.hProcess };
            const unique_handle thread_handle { created.hThread };
            // 자식만 사용해야 하는 handle 사본을 닫는다. 이 시점에 닫지 않으면 자식이
            // 끝나도 부모의 쓰기 end가 남아 reader가 EOF를 관측할 수 없다.
            input.reset();
            standard_output.write.reset();
            standard_error.write.reset();

            process_output_pipeline output_pipeline { process_stream::standard_output, request.maximum_record_bytes, request.maximum_captured_bytes_per_stream };
            process_output_pipeline error_pipeline { process_stream::standard_error, request.maximum_record_bytes, request.maximum_captured_bytes_per_stream };
            output_collector collector { sink };

            // pipe마다 전용 reader 스레드를 둔다. 한 스레드로 두 pipe를 읽으면 읽지 않는
            // pipe의 buffer가 가득 차서 자식이 멈출 수 있다.
            std::vector<std::thread> readers {};
            try
            {
                readers.reserve(2);
                readers.emplace_back([&standard_output, &output_pipeline, &collector]() { read_output_stream(standard_output.read.get(), output_pipeline, collector); });
                readers.emplace_back([&standard_error, &error_pipeline, &collector]() { read_output_stream(standard_error.read.get(), error_pipeline, collector); });
            }
            catch (...)
            {
                // reader가 없으면 자식이 pipe에서 막힐 수 있으므로 즉시 정리한다.
                terminate_child(process_handle.get());
                for (std::thread& reader : readers)
                    if (reader.joinable())
                        reader.join();
                return make_reader_failure_result(std::move(result), started);
            }

            // timeout과 취소 대기는 `S3-D4-CODE`에서 연결한다.
            SetLastError(ERROR_SUCCESS);
            const bool waited { WaitForSingleObject(process_handle.get(), INFINITE) == WAIT_OBJECT_0 };
            const std::uint32_t wait_error { waited ? std::uint32_t { ERROR_SUCCESS } : last_error_or(ERROR_INVALID_HANDLE) };
            if (waited == false)
            {
                // 결과를 신뢰할 수 없는 상태에서 자식을 남기면 orphan이 된다. 정리한 뒤
                // reader 스레드가 EOF를 보고 끝날 수 있게 한다.
                terminate_child(process_handle.get());
            }

            for (std::thread& reader : readers)
                reader.join();

            result.captured_bytes = output_pipeline.captured_bytes() + error_pipeline.captured_bytes();
            result.output_truncated = output_pipeline.truncated() || error_pipeline.truncated();
            result.record_count = collector.count();
            if (result.output_truncated)
            {
                result.diagnostics.push_back(
                    make_process_diagnostic(diagnostic_code::process_output_truncated, u8"출력이 스트림별 캡처 상한을 넘어 이후 내용을 버렸습니다.", std::nullopt, diagnostic_severity::warning));
            }
            if (collector.sink_failed())
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::operation_failed, u8"출력 sink가 예외를 던져 일부 레코드를 전달하지 못했습니다."));

            if (waited == false)
            {
                result.completion = process_completion::internal_error;
                result.native_error = wait_error;
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::operation_failed, u8"자식 프로세스의 종료를 기다리지 못해 강제로 종료했습니다.", result.native_error));
                finish_result(result, started);
                return result;
            }

            DWORD exit_code { 0 };
            SetLastError(ERROR_SUCCESS);
            if (GetExitCodeProcess(process_handle.get(), &exit_code) == FALSE)
            {
                result.completion = process_completion::internal_error;
                result.native_error = last_error_or(ERROR_INVALID_HANDLE);
                result.diagnostics.push_back(make_process_diagnostic(diagnostic_code::operation_failed, u8"자식 프로세스의 종료 코드를 읽지 못했습니다.", result.native_error));
                finish_result(result, started);
                return result;
            }

            result.completion = process_completion::exited;
            // Windows 종료 코드는 `0xC0000005`처럼 상위 bit를 쓰므로 bit 값을 보존한다.
            result.exit_code = static_cast<std::int32_t>(exit_code);
            finish_result(result, started);
            return result;
        }

        class win32_process_runner final : public process_runner
        {
        public:
            [[nodiscard]] process_result run(const process_request& request, process_output_sink* const sink, [[maybe_unused]] const process_cancellation_token& token) noexcept override
            {
                try
                {
                    return run_impl(request, sink);
                }
                catch (...)
                {
                    return make_unexpected_failure_result();
                }
            }
        };
    } // namespace

    std::unique_ptr<process_runner> make_process_runner()
    {
        return std::make_unique<win32_process_runner>();
    }
} // namespace gitman::win32
