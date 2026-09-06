#include "platform/win32/gitman_delegate.h"

#include "application/app_messages.h"
#include "platform/win32/gitman_driver.h"
#include "platform/win32/project_file_system.h"

#include "luil/ui/ui_interaction.h"
#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    // delegate가 host에 게시한 logic 메시지를 순서대로 적어 두는 driver다. 실제
    // gitman driver는 이 자리에서 게시 순서를 드러내지 않으므로(view만 보인다)
    // 조립 경계에서 무엇이 어떤 순서로 나가는지만 본다.
    class recording_driver final : public luil::win32::logic_driver
    {
    public:
        void handle(luil::app_message message) override
        {
            const gitman::logic_message* const payload { message.get<gitman::logic_message>() };
            if (payload == nullptr)
                return;
            // relay 깨우기는 내용이 없는 신호라 기록에서 뺀다.
            if (std::holds_alternative<gitman::relay_drain_intent>(*payload))
                return;
            if (std::holds_alternative<gitman::close_intent>(*payload))
                closed_.store(true);

            const std::lock_guard<std::mutex> lock { mutex_ };
            messages_.push_back(*payload);
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            return std::make_shared<luil::win32::ui_frame>();
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { gitman::logic_message { gitman::close_intent {} } };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed_.load();
        }

        [[nodiscard]] std::vector<gitman::logic_message> messages() const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return messages_;
        }

        [[nodiscard]] std::size_t count() const
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return messages_.size();
        }

    private:
        mutable std::mutex mutex_ {};
        std::vector<gitman::logic_message> messages_ {};
        std::atomic<bool> closed_ { false };
    };

    // delegate 하나와 그것이 게시할 host다. delegate는 실제 driver를 묶어야 하므로
    // 조립체를 하나 들되, 게시를 관찰하는 driver는 따로 둔다.
    struct delegate_harness
    {
        gitman::win32::gitman_driver bound_driver {};
        recording_driver recorder {};
        luil::interaction_policy policy {};
        luil::win32::app_host host { luil::win32::app_host::config {}, recorder, &policy };
        gitman::win32::gitman_delegate delegate;

        explicit delegate_harness(std::optional<std::u8string> startup_document = {})
            : delegate { bound_driver, std::move(startup_document) }
        {}

        delegate_harness(const delegate_harness&) = delete;
        delegate_harness(delegate_harness&&) = delete;
        delegate_harness& operator=(const delegate_harness&) = delete;
        delegate_harness& operator=(delegate_harness&&) = delete;

        ~delegate_harness()
        {
            host.shutdown();
            // `bound_driver`는 이 host의 driver가 아니라 종료 순서가 닿지 않는다.
            // relay가 곧 사라질 host를 가리킨 채 남지 않도록 직접 놓는다.
            bound_driver.stop_workers();
        }

        [[nodiscard]] bool wait_for_count(const std::size_t expected) const
        {
            const auto deadline { std::chrono::steady_clock::now() + 5s };
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (recorder.count() >= expected)
                    return true;
                std::this_thread::sleep_for(1ms);
            }
            return recorder.count() >= expected;
        }

        // 더 오지 않는 것을 확인하는 짧은 정지다.
        void settle() const
        {
            std::this_thread::sleep_for(60ms);
        }
    };
} // namespace

TEST_CASE("Window placement converts between the luil and gitman shapes", "[win32][delegate]")
{
    // 두 타입의 필드는 같고 정수 폭만 다르다. 저장·복원이 값을 잃지 않아야 한다.
    const gitman::window_placement original { 120, 80, 1280, 720, true };
    const luil::win32::window_placement converted { gitman::win32::to_luil_placement(original) };
    REQUIRE(converted.x == 120);
    REQUIRE(converted.y == 80);
    REQUIRE(converted.width == 1280);
    REQUIRE(converted.height == 720);
    REQUIRE(converted.maximized);
    REQUIRE(gitman::win32::to_gitman_placement(converted) == original);

    // 최대화가 아닌 값과 음수 좌표(보조 모니터)도 그대로 왕복한다.
    const gitman::window_placement negative { -1400, -200, 800, 600, false };
    REQUIRE(gitman::win32::to_gitman_placement(gitman::win32::to_luil_placement(negative)) == negative);

    // 유효성 판정도 두 쪽이 같은 뜻이다 (크기가 양수여야 복원에 쓴다).
    REQUIRE(negative.valid());
    REQUIRE(gitman::win32::to_luil_placement(negative).valid());
    const gitman::window_placement empty {};
    REQUIRE(empty.valid() == false);
    REQUIRE(gitman::win32::to_luil_placement(empty).valid() == false);
}

TEST_CASE("Window metrics become a window metrics intent", "[win32][delegate]")
{
    delegate_harness harness {};
    const luil::app_message message { harness.delegate.make_window_metrics_message(1024.0f, 768.0f, 1.5f) };
    const gitman::logic_message* const payload { message.get<gitman::logic_message>() };
    REQUIRE(payload != nullptr);
    const auto* const intent { std::get_if<gitman::window_metrics_intent>(payload) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->width == 1024.0f);
    REQUIRE(intent->height == 768.0f);
    REQUIRE(intent->scale == 1.5f);
}

TEST_CASE("A valid window placement becomes an intent and an invalid one becomes nothing", "[win32][delegate]")
{
    delegate_harness harness {};

    const luil::app_message valid { harness.delegate.make_window_placement_message(luil::win32::window_placement { 10, 20, 640, 480, false }) };
    const gitman::logic_message* const payload { valid.get<gitman::logic_message>() };
    REQUIRE(payload != nullptr);
    const auto* const intent { std::get_if<gitman::window_placement_intent>(payload) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->placement == gitman::window_placement { 10, 20, 640, 480, false });

    // 크기가 양수가 아니면 복원에 쓸 수 없다. 빈 메시지는 게시되지 않는다.
    const luil::app_message invalid { harness.delegate.make_window_placement_message(luil::win32::window_placement { 10, 20, 0, 480, false }) };
    REQUIRE(invalid.empty());
    REQUIRE(invalid.get<gitman::logic_message>() == nullptr);
}

TEST_CASE("Starting posts the startup intent and the font families in order", "[win32][delegate]")
{
    // logic thread의 `start()`가 아니라 이 게시가 시작을 연다 (설계 L3). 글꼴
    // 열거는 OS 호출이라 UI thread만 할 수 있어 함께 실린다.
    delegate_harness harness {};
    harness.delegate.on_started(harness.host);

    REQUIRE(harness.wait_for_count(2));
    harness.settle();
    const std::vector<gitman::logic_message> messages { harness.recorder.messages() };
    REQUIRE(messages.size() == 2u);
    REQUIRE(std::holds_alternative<gitman::startup_intent>(messages[0]));
    REQUIRE(std::holds_alternative<gitman::font_families_event>(messages[1]));
}

TEST_CASE("A startup document is opened as an absolute path after the startup messages", "[win32][delegate]")
{
    // 명령줄이 준 문서다. 상대 경로 그대로 열면 문서 기준 상대 저장소 경로가
    // 엉뚱한 곳을 가리키므로 절대 경로로 펴서 게시한다.
    const std::u8string relative { u8"startup.version-list" };
    delegate_harness harness { relative };
    harness.delegate.on_started(harness.host);

    REQUIRE(harness.wait_for_count(3));
    harness.settle();
    const std::vector<gitman::logic_message> messages { harness.recorder.messages() };
    REQUIRE(messages.size() == 3u);
    REQUIRE(std::holds_alternative<gitman::startup_intent>(messages[0]));
    REQUIRE(std::holds_alternative<gitman::font_families_event>(messages[1]));
    const auto* const open { std::get_if<gitman::open_document_intent>(&messages[2]) };
    REQUIRE(open != nullptr);
    REQUIRE(open->path == gitman::win32::absolute_workspace_document_path(relative));
    REQUIRE(open->path != relative);
}

TEST_CASE("Dropping a document file opens it and other files are declined", "[win32][delegate]")
{
    delegate_harness harness {};

    // 받는 element가 없는 드롭의 물러섬이다. 문서만 받고 나머지는 그대로 흘린다.
    REQUIRE(harness.delegate.on_file_dropped(harness.host, std::u8string { u8"C:\\work\\notes.txt" }) == false);
    REQUIRE(harness.delegate.on_file_dropped(harness.host, std::u8string { u8"C:\\work" }) == false);
    harness.settle();
    REQUIRE(harness.recorder.count() == 0u);

    const std::u8string dropped { u8"work\\dropped.version-list" };
    REQUIRE(harness.delegate.on_file_dropped(harness.host, dropped));
    REQUIRE(harness.wait_for_count(1));
    const std::vector<gitman::logic_message> messages { harness.recorder.messages() };
    REQUIRE(messages.size() == 1u);
    const auto* const open { std::get_if<gitman::open_document_intent>(&messages[0]) };
    REQUIRE(open != nullptr);
    REQUIRE(open->path == gitman::win32::absolute_workspace_document_path(dropped));
}
