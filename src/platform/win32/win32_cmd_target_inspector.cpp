#include "platform/win32/win32_cmd_target_inspector.h"

#include "infrastructure/cmd_target_inspection.h"
#include "infrastructure/command_line_builder.h"
#include "platform/win32/utf8.h"
#include "platform/win32/win32_file_association.h"
#include "platform/win32/win32_vcs_file_probe.h"

#include <nlohmann/json.hpp>
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gitman::win32 {
    namespace {
        constexpr std::u8string_view probe_mode { u8"--internal-cmd-target-probe" };
        constexpr DWORD protocol_magic { 0x474d4351 };
        constexpr std::size_t request_capacity { 1024u * 1024u };

        // 포인터를 공유하지 않는 고정 배치다. 결과를 다 쓴 뒤 event로 부모에게 공개한다.
        struct probe_block
        {
            DWORD magic {};
            DWORD request_bytes {};
            DWORD status {};
            DWORD path_exists {};
            DWORD batch_present {};
            DWORD delayed_expansion {};
            DWORD calls {};
            DWORD unchecked {};
            std::array<char, request_capacity> request {};
        };

        struct close_handle
        {
            void operator()(void* handle) const noexcept
            {
                if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                    CloseHandle(handle);
            }
        };
        using owned_handle = std::unique_ptr<void, close_handle>;

        struct unmap_view
        {
            void operator()(void* address) const noexcept
            {
                if (address != nullptr)
                    UnmapViewOfFile(address);
            }
        };
        using owned_view = std::unique_ptr<void, unmap_view>;

        struct delete_attributes
        {
            void operator()(LPPROC_THREAD_ATTRIBUTE_LIST attributes) const noexcept
            {
                DeleteProcThreadAttributeList(attributes);
            }
        };

        std::string bytes(const std::u8string_view value)
        {
            return { reinterpret_cast<const char*>(value.data()), value.size() };
        }

        std::u8string utf8(const nlohmann::json& value)
        {
            const std::string text { value.get<std::string>() };
            return { text.begin(), text.end() };
        }

        std::string encode(const cmd_target_query& query)
        {
            nlohmann::json value {
                { "directory", bytes(query.working_directory) }
                , { "argument", query.argument }
                , { "sentence", query.sentence }
                , { "path_batch", bytes(query.batch_if_path) }
                , { "sentence_batch", bytes(query.batch_if_sentence) }
                , { "relative", nullptr }
            };
            if (query.relative_path.has_value())
                value["relative"] = bytes(*query.relative_path);
            return value.dump();
        }

        cmd_target_query decode(const probe_block& block)
        {
            const auto value { nlohmann::json::parse(block.request.data(), block.request.data() + block.request_bytes) };
            cmd_target_query query {};
            query.working_directory = utf8(value.at("directory"));
            query.argument = value.at("argument").get<std::size_t>();
            query.sentence = value.at("sentence").get<bool>();
            query.batch_if_path = utf8(value.at("path_batch"));
            query.batch_if_sentence = utf8(value.at("sentence_batch"));
            if (value.at("relative").is_null() == false)
                query.relative_path = utf8(value.at("relative"));
            return query;
        }

        std::optional<HANDLE> parse_handle(const std::u8string& value)
        {
            std::uintptr_t number {};
            const std::string text { bytes(value) };
            const auto parsed { std::from_chars(text.data(), text.data() + text.size(), number) };
            if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size() || number == 0)
                return std::nullopt;
            return reinterpret_cast<HANDLE>(number);
        }

        std::u8string handle_argument(const HANDLE value)
        {
            const std::string number { std::to_string(reinterpret_cast<std::uintptr_t>(value)) };
            return { number.begin(), number.end() };
        }

        class isolated_cmd_target_inspector final : public cmd_target_inspector
        {
        public:
            isolated_cmd_target_inspector(std::u8string executable, const std::chrono::milliseconds timeout)
                : executable_ { std::move(executable) }
                , timeout_ { timeout }
            {
            }

            ~isolated_cmd_target_inspector() override
            {
                terminate();
            }

            cmd_inspection_result inspect(const cmd_target_query& query, const process_cancellation_token& token) override
            {
                const std::lock_guard<std::mutex> lock { mutex_ };
                cmd_inspection_result result {};
                result.facts.query = query;
                if (token.cancelled())
                {
                    result.outcome = cmd_inspection_outcome::cancelled;
                    return result;
                }
                // OS에 취소를 요청해도 kernel I/O 회수는 늦을 수 있다. 남아 있는 동안 다음 자식을 늘리지 않는다.
                if (process_ != nullptr && WaitForSingleObject(process_.get(), 50) != WAIT_OBJECT_0)
                {
                    if (token.cancelled())
                        result.outcome = cmd_inspection_outcome::cancelled;
                    return result;
                }
                process_.reset();
                job_.reset();
                try
                {
                    return inspect_once(query, token);
                }
                catch (...)
                {
                    terminate();
                    return result;
                }
            }

        private:
            void terminate() noexcept
            {
                if (job_ != nullptr)
                    TerminateJobObject(job_.get(), ERROR_CANCELLED);
                if (process_ != nullptr && WaitForSingleObject(process_.get(), 0) != WAIT_OBJECT_0)
                    TerminateProcess(process_.get(), ERROR_CANCELLED);
            }

            cmd_inspection_result inspect_once(const cmd_target_query& query, const process_cancellation_token& token)
            {
                cmd_inspection_result result {};
                result.facts.query = query;
                // 조회 대상과 달리 실행 파일은 로컬 드라이브의 절대 경로만 쓴다. UNC exe를 열며 부모가 멈추면 격리 뜻이 없다.
                const std::u8string_view absolute { executable_.starts_with(u8"\\\\?\\")
                    ? std::u8string_view { executable_ }.substr(4) : std::u8string_view { executable_ }
                };
                if (timeout_.count() <= 0 || absolute.size() < 3 || absolute[1] != u8':'
                    || (absolute[2] != u8'\\' && absolute[2] != u8'/'))
                    return result;
                const std::string request { encode(query) };
                if (request.size() > request_capacity)
                    return result;
                SECURITY_ATTRIBUTES security { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
                owned_handle mapping { CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(probe_block)), nullptr) };
                owned_handle response { CreateEventW(&security, TRUE, FALSE, nullptr) };
                owned_handle cancelled { CreateEventW(nullptr, TRUE, FALSE, nullptr) };
                if (mapping == nullptr || response == nullptr || cancelled == nullptr)
                    return result;
                owned_view memory { MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(probe_block)) };
                if (memory == nullptr)
                    return result;
                auto* const block { ::new (memory.get()) probe_block {} };
                block->magic = protocol_magic;
                block->request_bytes = static_cast<DWORD>(request.size());
                std::memcpy(block->request.data(), request.data(), request.size());

                const auto wide_executable { utf8_to_utf16(executable_) };
                if (wide_executable.value.has_value() == false)
                    return result;
                const std::vector<std::u8string> arguments { std::u8string { probe_mode }, handle_argument(mapping.get()), handle_argument(response.get()) };
                auto command { utf8_to_utf16(build_windows_command_line(executable_, arguments)) };
                if (command.value.has_value() == false)
                    return result;
                SIZE_T attribute_bytes {};
                InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
                std::vector<std::byte> attribute_storage(attribute_bytes);
                auto* const list { reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data()) };
                if (InitializeProcThreadAttributeList(list, 1, 0, &attribute_bytes) == FALSE)
                    return result;
                const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, delete_attributes> attributes { list };
                HANDLE inherited[] { mapping.get(), response.get() };
                if (UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) == FALSE)
                    return result;
                STARTUPINFOEXW startup {};
                startup.StartupInfo.cb = sizeof(startup);
                startup.lpAttributeList = list;
                PROCESS_INFORMATION information {};
                job_.reset(CreateJobObjectW(nullptr, nullptr));
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (job_ == nullptr || SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE)
                    return result;
                const DWORD flags { CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT };
                // 작업 폴더를 조회 대상 UNC로 두지 않는다. 자식이 시작한 뒤 대상 파일을 연다.
                const auto separator { wide_executable.value->find_last_of(L"\\/") };
                const std::wstring directory { wide_executable.value->substr(0, separator + 1) };
                if (CreateProcessW(wide_executable.value->c_str(), command.value->data(), nullptr, nullptr, TRUE, flags
                    , nullptr, directory.c_str(), &startup.StartupInfo, &information) == FALSE)
                    return result;
                process_.reset(information.hProcess);
                const owned_handle thread { information.hThread };
                if (AssignProcessToJobObject(job_.get(), process_.get()) == FALSE)
                {
                    terminate();
                    return result;
                }
                const HANDLE cancel_event { cancelled.get() };
                const auto registration { token.register_callback([cancel_event]
                    {
                        SetEvent(cancel_event);
                    })
                };
                if (token.cancelled() || ResumeThread(thread.get()) == static_cast<DWORD>(-1))
                {
                    terminate();
                    result.outcome = token.cancelled() ? cmd_inspection_outcome::cancelled : cmd_inspection_outcome::failed;
                    return result;
                }
                const HANDLE waiting[] { response.get(), process_.get(), cancelled.get() };
                const DWORD wait { WaitForMultipleObjects(3, waiting, FALSE, static_cast<DWORD>(std::min<std::int64_t>(timeout_.count(), MAXDWORD - 1))) };
                if (token.cancelled() || wait == WAIT_OBJECT_0 + 2)
                    result.outcome = cmd_inspection_outcome::cancelled;
                else if (wait == WAIT_TIMEOUT)
                    result.outcome = cmd_inspection_outcome::timed_out;
                else if (wait == WAIT_OBJECT_0 && block->magic == protocol_magic && block->status == 1
                    && block->path_exists <= 1 && block->batch_present <= 1 && block->delayed_expansion <= 1 && block->calls <= 1 && block->unchecked <= 1)
                {
                    result.outcome = cmd_inspection_outcome::completed;
                    result.facts.path_exists = block->path_exists != 0;
                    if (block->batch_present)
                        result.facts.batch = cmd_batch_findings { block->delayed_expansion != 0, block->calls != 0, block->unchecked != 0 };
                }
                terminate();
                return result;
            }

            std::u8string executable_ {};
            std::chrono::milliseconds timeout_ {};
            std::mutex mutex_ {};
            owned_handle process_ {};
            owned_handle job_ {};
        };
    }

    std::unique_ptr<cmd_target_inspector> make_cmd_target_inspector(std::u8string executable, const std::chrono::milliseconds timeout)
    {
        if (executable.empty())
            executable = current_executable_path();
        return std::make_unique<isolated_cmd_target_inspector>(std::move(executable), timeout);
    }

    std::optional<int> run_cmd_target_probe(const std::span<const std::u8string> arguments)
    {
        if (arguments.size() < 2 || arguments[1] != probe_mode)
            return std::nullopt;
        if (arguments.size() != 4)
            return 2;
        const auto mapping_argument { parse_handle(arguments[2]) };
        const auto event_argument { parse_handle(arguments[3]) };
        if (mapping_argument.has_value() == false || event_argument.has_value() == false)
            return 2;
        const owned_handle mapping { *mapping_argument };
        const owned_handle response { *event_argument };
        const owned_view memory { MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(probe_block)) };
        if (memory == nullptr)
            return 3;
        auto* const block { static_cast<probe_block*>(memory.get()) };
        if (block->magic != protocol_magic || block->request_bytes == 0 || block->request_bytes > request_capacity)
            return 3;
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
        try
        {
            const cmd_target_query query { decode(*block) };
            const auto probe { make_vcs_file_probe() };
            const cmd_target_facts facts { inspect_cmd_target(*probe, query) };
            block->path_exists = facts.path_exists;
            block->batch_present = facts.batch.has_value();
            if (facts.batch.has_value())
            {
                block->delayed_expansion = facts.batch->delayed_expansion;
                block->calls = facts.batch->call;
                block->unchecked = facts.batch->unchecked;
            }
            block->status = 1;
        }
        catch (...)
        {
            block->status = 2;
        }
        MemoryBarrier();
        return SetEvent(response.get()) != FALSE ? 0 : 4;
    }
}
