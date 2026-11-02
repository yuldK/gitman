#pragma once

// 카드 기능 열과 기능 메뉴 test(function-buttons-design T9)가 함께 쓰는 도우미다. 기능 버튼 절이 든 문서를 연
// logic controller에서 view snapshot을 받아 실제 tree를 짓는다 — 카드 view(판정 결과)와 element가 한 경로를 지난다.

#include "helpers/ui_test_support.h"

#include "application/cancellation_registry.h"
#include "application/logic_controller.h"
#include "domain/function_trust.h"
#include "infrastructure/function_execution_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gitman::testing {
    class function_card_submitter final : public operation_submitter
    {
    public:
        [[nodiscard]] bool submit(operation_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<operation_request> requests {};
    };

    class function_card_run_submitter final : public function_run_submitter
    {
    public:
        [[nodiscard]] bool submit(function_run_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<function_run_request> requests {};
    };

    [[nodiscard]] inline project_definition make_function_card_project(const std::u8string_view id)
    {
        project_definition project {};
        project.id.value = id;
        project.display_name = id;
        project.path.original = std::u8string { u8"C:\\work\\" } + std::u8string { id };
        project.path.normalized = project.path.original;
        // 문서를 읽을 때 resolver가 채우는 값이다. `${projectRoot}`(기본 cwd)를 쓰는 기능이 활성이려면 필요하다.
        project.path.state = configured_path_state::available;
        return project;
    }

    // 런타임 변수를 쓰지 않아 조회 전에도 활성인 출력 수집 버튼이다.
    [[nodiscard]] inline function_button make_function_card_button(const std::u8string_view id, const std::u8string_view label, const std::optional<std::u8string_view> placement = std::nullopt)
    {
        function_button button {};
        button.id = id;
        button.label = label;
        button.program = u8"C:\\Tools\\run.exe";
        if (placement.has_value())
            button.placement = std::u8string { *placement };
        return button;
    }

    // `p00`, `p01`… 이름의 project다.
    [[nodiscard]] inline std::vector<project_definition> make_function_card_projects(const std::size_t count)
    {
        std::vector<project_definition> projects {};
        for (std::size_t index = 0; index < count; ++index)
        {
            std::u8string id { u8"p" };
            id += static_cast<char8_t>(u8'0' + index / 10);
            id += static_cast<char8_t>(u8'0' + index % 10);
            projects.push_back(make_function_card_project(id));
        }
        return projects;
    }

    // 문서를 열고 기능 실행 경계를 묶은 controller다. 신뢰를 기록한 정의로 시작해 실행 intent가 곧바로 제출된다
    // (신뢰 확인 흐름은 T7 test가 본다).
    struct function_card_fixture
    {
        function_card_submitter submitter {};
        function_card_run_submitter functions {};
        cancellation_registry registry {};
        logic_controller controller { submitter, registry };

        function_card_fixture(std::vector<project_definition> projects, std::vector<function_button> buttons, const float width = 1280.0f, const float height = 720.0f)
        {
            function_buttons_definition definition {};
            for (function_button& button : buttons)
                definition.buttons.emplace_back(std::move(button));

            controller.bind_function_execution(functions
                , make_function_run_services(
                    [](const std::u8string_view) -> std::optional<std::u8string>
                    {
                        return std::nullopt;
                    }
                )
            );
            controller.handle(window_metrics_intent { width, height, 1.0f });
            controller.start();
            app_settings_loaded_event settings {};
            settings.operation_id = submitter.requests.back().operation_id;
            settings.settings.trusted_function_definitions.push_back({ std::u8string { document_path }, function_trust_digest(definition, projects) });
            controller.handle(std::move(settings));

            controller.handle(open_document_intent { std::u8string { document_path } });
            document_loaded_event event {};
            workspace_document document {};
            document.document_path = document_path;
            document.projects = std::move(projects);
            document.function_buttons = std::move(definition);
            event.document = { std::move(document) };
            controller.handle(std::move(event));
            submitter.requests.clear();
        }

        // 로컬 조회 결과다. update·switch가 준비 상태가 된다.
        void query(const std::u8string_view id)
        {
            query_completed_event event {};
            event.id.value = id;
            event.generation = 1;
            event.final_event = true;
            event.result.snapshot.project.value = id;
            event.result.snapshot.kind = repository_kind::git;
            event.result.snapshot.availability = repository_availability::ready;
            event.result.snapshot.current_reference = u8"main";
            event.result.snapshot.local_revision = u8"a1b2c3d";
            event.result.snapshot.working_tree.state = working_tree_state::clean;
            controller.handle(std::move(event));
            submitter.requests.clear();
        }

        [[nodiscard]] std::shared_ptr<const view_snapshot> snapshot() const
        {
            return controller.make_view_snapshot();
        }

        [[nodiscard]] std::shared_ptr<const luil::ui_tree> tree() const
        {
            return make_tree(*snapshot());
        }

        [[nodiscard]] card_view_model card(const std::u8string_view id) const
        {
            const auto view { snapshot() };
            const card_view_model* found { nullptr };
            for (const card_view_model& model : view->cards)
            {
                if (model.id.value == id)
                    found = &model;
            }
            REQUIRE(found != nullptr);
            return *found;
        }

        void run(const std::u8string_view id, const std::u8string_view button)
        {
            controller.handle(run_function_intent { project_id { std::u8string { id } }, std::u8string { button } });
        }

        void started(const function_run_request& request, const std::chrono::steady_clock::time_point at)
        {
            controller.handle(function_started_event { request.id, request.run_id, at });
        }

        void completed(const function_run_request& request, const function_run_outcome outcome, const std::optional<std::int32_t> exit_code = std::nullopt)
        {
            function_completed_event event {};
            event.id = request.id;
            event.run_id = request.run_id;
            event.outcome = outcome;
            event.exit_code = exit_code;
            controller.handle(std::move(event));
        }

        // 열린 메뉴다. 없으면 실패다.
        [[nodiscard]] context_menu_view menu() const
        {
            const auto view { snapshot() };
            REQUIRE(view->context_menu.has_value());
            return *view->context_menu;
        }

        static constexpr std::u8string_view document_path { u8"C:\\work\\projects.version-list" };
    };

    // 카드 메뉴·기능 메뉴의 기능 항목 수다.
    [[nodiscard]] inline std::size_t function_item_count(const context_menu_view& menu)
    {
        std::size_t count { 0 };
        for (const context_menu_item_view& item : menu.items)
        {
            if (item.entry == context_menu_entry::run_function || item.entry == context_menu_entry::stop_function)
                ++count;
        }
        return count;
    }
}
