#include "application/logic_controller.h"
#include "application/task_scheduler.h"
#include "messaging/channel.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {
    constexpr std::size_t stress_card_count { 8 };
    constexpr int stress_log_batches { 50 };
    constexpr int stress_entries_per_batch { 4 };

    // 요청 종류마다 즉시 결과를 만들어 내는 executor 대역이다. update는 카드 id가
    // 박힌 로그 배치를 대량으로 쏟아 내 병렬 worker의 로그 격리를 시험한다.
    class chatty_executor final : public gitman::operation_executor
    {
    public:
        void execute(const gitman::operation_request& request, const std::function<void(gitman::logic_message)>& emit) noexcept override
        {
            try
            {
                if (request.kind == gitman::operation_kind::load_document)
                {
                    gitman::document_loaded_event event {};
                    event.operation_id = request.operation_id;
                    gitman::workspace_document document {};
                    document.document_path = request.document_path;
                    for (std::size_t index = 0; index < stress_card_count; ++index)
                    {
                        gitman::project_definition project {};
                        project.id.value = card_name(index);
                        project.display_name = project.id.value;
                        project.path.original = std::u8string { u8"C:\\work\\" } + project.id.value;
                        project.path.normalized = project.path.original;
                        document.projects.push_back(std::move(project));
                    }
                    event.document = { std::move(document) };
                    emit(std::move(event));
                    return;
                }

                if (request.kind == gitman::operation_kind::query_local || request.kind == gitman::operation_kind::refresh)
                {
                    gitman::query_completed_event event {};
                    event.operation_id = request.operation_id;
                    event.generation = request.generation;
                    event.id = request.project.id;
                    event.final_event = true;
                    event.result.snapshot.project = request.project.id;
                    event.result.snapshot.kind = gitman::repository_kind::git;
                    event.result.snapshot.availability = gitman::repository_availability::ready;
                    event.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
                    emit(std::move(event));
                    return;
                }

                if (request.kind == gitman::operation_kind::update)
                {
                    for (int batch = 0; batch < stress_log_batches; ++batch)
                    {
                        gitman::operation_log_event log {};
                        log.operation_id = request.operation_id;
                        log.id = request.project.id;
                        for (int entry = 0; entry < stress_entries_per_batch; ++entry)
                        {
                            gitman::operation_log_entry value {};
                            value.kind = gitman::log_entry_kind::standard_output;
                            value.text = request.project.id.value;
                            log.entries.push_back(std::move(value));
                        }
                        emit(std::move(log));
                    }

                    gitman::change_completed_event done {};
                    done.operation_id = request.operation_id;
                    done.generation = request.generation;
                    done.id = request.project.id;
                    done.kind = gitman::operation_kind::update;
                    done.result.executed = true;
                    done.result.succeeded = true;
                    done.result.snapshot.project = request.project.id;
                    done.result.snapshot.availability = gitman::repository_availability::ready;
                    done.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
                    emit(std::move(done));
                    return;
                }
            }
            catch (...)
            {}
        }

        [[nodiscard]] static std::u8string card_name(const std::size_t index)
        {
            std::u8string name { u8"stress-card-" };
            name.push_back(static_cast<char8_t>(u8'0' + index));
            return name;
        }
    };

    // 도착한 메시지를 기한까지 controller에 먹이며 조건을 기다린다.
    bool drain_until(messaging::channel<gitman::logic_message>& inbox, gitman::logic_controller& controller, const std::function<bool()>& done)
    {
        const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 30 } };
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (done())
                return true;
            messaging::envelope<gitman::logic_message> received {};
            if (inbox.receive_wait(received, std::chrono::milliseconds { 50 }) == messaging::receive_status::received)
                controller.handle(std::move(received.payload));
        }
        return done();
    }

    bool all_cards_idle(gitman::logic_controller& controller)
    {
        const auto view { controller.make_view_snapshot() };
        if (view->cards.size() != stress_card_count)
            return false;
        for (const gitman::card_view_model& card : view->cards)
            if (card.busy || card.state != gitman::card_view_state::ready)
                return false;
        return true;
    }
} // namespace

TEST_CASE("Parallel change operations never mix their card logs", "[logic][scheduler][stress]")
{
    chatty_executor executor {};
    messaging::channel<gitman::logic_message> inbox { messaging::channel_options { 256, messaging::overflow_policy::reject_newest, {} } };
    gitman::task_scheduler scheduler { executor, inbox, 4 };
    gitman::logic_controller controller { scheduler };

    controller.handle(gitman::open_document_intent { u8"C:\\work\\stress.version-list" });
    REQUIRE(drain_until(inbox, controller, [&controller] { return all_cards_idle(controller); }));

    // 8개 카드의 update를 한꺼번에 요청한다. 4개 worker가 병렬로 로그를 쏟아 낸다.
    for (std::size_t index = 0; index < stress_card_count; ++index)
        controller.handle(gitman::request_update_intent { gitman::project_id { chatty_executor::card_name(index) }, {} });

    REQUIRE(drain_until(inbox, controller, [&controller] { return all_cards_idle(controller); }));
    scheduler.shutdown();

    // 각 카드 buffer에는 자기 로그만, 빠짐없이, sequence 순서대로 남는다 (REQ-008).
    for (std::size_t index = 0; index < stress_card_count; ++index)
    {
        const std::u8string name { chatty_executor::card_name(index) };
        const gitman::operation_log_buffer* const buffer { controller.card_log(gitman::project_id { name }) };
        REQUIRE(buffer != nullptr);

        // 시작 1 + 출력 200 + 완료 1 = 202이며 상한(1000)에 걸리지 않는다.
        REQUIRE(buffer->records().size() == static_cast<std::size_t>(stress_log_batches * stress_entries_per_batch) + 2u);
        REQUIRE(buffer->dropped_count() == 0u);

        std::uint64_t previous_sequence { 0 };
        for (const gitman::operation_log_record& record : buffer->records())
        {
            REQUIRE(record.sequence > previous_sequence);
            previous_sequence = record.sequence;
            if (record.entry.kind == gitman::log_entry_kind::standard_output)
                REQUIRE(record.entry.text == name);
        }
    }
}
