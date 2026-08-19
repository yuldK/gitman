#pragma once

#include "domain/project.h"
#include "presentation/ui/ui_events.h"
#include "presentation/ui_theme.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

class SkCanvas;
class SkTypeface;

namespace gitman::ui {
    struct rect_f
    {
        float x { 0.0f };
        float y { 0.0f };
        float width { 0.0f };
        float height { 0.0f };

        [[nodiscard]] bool contains(float point_x, float point_y) const noexcept;
    };

    // element의 종류다. 종류 + 소속 project id가 tree 안의 안정적인 정체성이 되어
    // snapshot이 다시 빌드되어도 hover·pressed·tooltip이 같은 대상으로 이어진다.
    enum class ui_element_kind
    {
        none,
        root,
        caption,
        caption_title,
        caption_minimize,
        caption_maximize,
        caption_close,
        toolbar,
        toolbar_document_path,
        toolbar_refresh_all,
        toolbar_open_document,
        toolbar_generate_document,
        toolbar_toggle_path_display,
        toolbar_settings,
        toolbar_discover,
        notice,
        card_list,
        card_scrollbar,
        card_body,
        card_refresh,
        card_update,
        card_switch,
        empty_state,
        // 선택 카드 전용 하단 로그 pane과 헤더 버튼들이다 (단계 7).
        log_pane,
        log_title,
        log_filter,
        log_autoscroll,
        log_copy,
        log_clear,
        // Git update 확인 overlay다 (단계 7). 배경 클릭은 닫기이고 panel이 클릭을
        // 흡수한다.
        update_overlay,
        update_overlay_panel,
        update_overlay_submodule,
        update_overlay_confirm,
        update_overlay_cancel,
        // switch dialog다 (단계 7). 후보 행의 정체성은 owner의 값에 index를 담아
        // 구분한다.
        switch_dialog,
        switch_dialog_panel,
        switch_dialog_item,
        switch_dialog_confirm,
        switch_dialog_cancel,
        // 환경설정 dialog다 (REQ-017, 단계 8). 배경 클릭은 닫기이고 panel이 클릭을
        // 흡수한다.
        settings_dialog,
        settings_dialog_panel,
        settings_git_browse,
        settings_git_clear,
        settings_svn_browse,
        settings_svn_clear,
        settings_dialog_confirm,
        settings_dialog_cancel,
        // 탐색 후보 선택 등록 dialog다 (REQ-004, 단계 8). 후보 행의 정체성은
        // switch dialog처럼 owner의 값에 index를 담아 구분한다.
        discovery_dialog,
        discovery_dialog_panel,
        discovery_dialog_item,
        discovery_dialog_confirm,
        discovery_dialog_cancel,
    };

    struct ui_element_id
    {
        ui_element_kind kind { ui_element_kind::none };
        project_id owner {};

        [[nodiscard]] bool operator==(const ui_element_id&) const = default;
    };

    enum class ui_trigger
    {
        left_click,
        right_click,
        double_click,
    };

    inline constexpr std::size_t ui_trigger_count { 3 };

    // 액션 실행 시점의 문맥이다. 좌표는 물리 픽셀(창 좌표)이다.
    struct ui_action_context
    {
        ui_element_id element {};
        float x { 0.0f };
        float y { 0.0f };
        bool control { false };
    };

    // 액션은 상태를 바꾸지 않고 후속 메시지를 반환한다 (ADR-004).
    using ui_action = std::function<std::vector<input_action>(const ui_action_context&)>;

    struct drag_payload
    {
        ui_element_id source {};
        project_id dragged_project {};
        // drag ghost에 표시할 이름이다. 비어 있으면 project id를 대신 쓴다.
        std::u8string label {};

        [[nodiscard]] bool operator==(const drag_payload&) const = default;
    };

    struct drag_source
    {
        std::function<drag_payload(const ui_action_context&)> make_payload {};
    };

    struct drop_target
    {
        std::function<bool(const drag_payload&)> accepts {};
        std::function<std::vector<input_action>(const drag_payload&, const ui_action_context&)> on_drop {};
    };

    // 누른 채 끄는 동안 연속으로 반응하는 element다 (스크롤 막대). 카드의
    // drag & drop과 달리 ghost도 drop 대상도 없고, 포인터 이동을 그때그때 메시지로
    // 바꾼다. 눌린 동안에는 포인터가 element를 벗어나도 계속 호출된다.
    struct pointer_drag_target
    {
        // 눌린 순간 한 번이다. 누른 지점으로 즉시 이동할지 element가 정한다.
        std::function<std::vector<input_action>(const ui_action_context&)> on_press {};
        // 직전 위치와 현재 위치를 받는다. 상대 변화량만 쓰므로 tree가 다시 빌드되어도
        // 이어서 끌 수 있다.
        std::function<std::vector<input_action>(const ui_action_context& previous, const ui_action_context& current)> on_move {};
    };

    // 배치 문맥이다. slot은 부모가 준 영역이고 scroll_offset은 논리 픽셀이다.
    struct arrange_context
    {
        rect_f slot {};
        float scale { 1.0f };
        float scroll_offset { 0.0f };
    };

    // 그리기 문맥이다. draw 호출 동안만 유효하다. now는 tooltip 지연 판정용이고
    // maximized는 view snapshot에 없는 창 상태라 UI thread가 채운다.
    struct draw_context
    {
        SkCanvas& canvas;
        SkTypeface* codicon_typeface { nullptr };
        SkTypeface* ui_typeface { nullptr };
        const ui_color_palette& palette;
        float scale { 1.0f };
        std::chrono::steady_clock::time_point now {};
        bool maximized { false };
    };

    struct drag_visual
    {
        drag_payload payload {};
        float x { 0.0f };
        float y { 0.0f };
        // 현재 위치에서 payload를 수락하는 drop 대상이다. 강조 표시에 쓴다.
        ui_element_id hovered_drop_target {};

        [[nodiscard]] bool operator==(const drag_visual&) const = default;
    };

    // input thread가 게시하고 UI thread가 그리기에 쓰는 상호작용 발행본이다.
    // 업무 상태가 아니라 ADR-004의 "입력 정규화 상태"다.
    struct interaction_snapshot
    {
        ui_element_id hovered {};
        ui_element_id pressed {};
        // hover가 시작된 시각이다. tooltip 표시 여부(지연 경과)는 그리는 쪽이
        // 판정한다.
        std::optional<std::chrono::steady_clock::time_point> hover_started_at {};
        std::optional<drag_visual> drag {};

        [[nodiscard]] bool operator==(const interaction_snapshot&) const = default;
    };

    inline constexpr std::chrono::milliseconds tooltip_delay { 500 };

    // 모든 화면 요소의 최상위 추상 클래스다 (docs/ui-element-design.md). 빌드 중에만
    // mutable이고 tree로 게시된 뒤에는 불변으로 취급하므로 여러 스레드가 동시에
    // 읽어도 안전하다. "재설정"은 다음 snapshot의 tree 빌드에서 다른 값을 등록하는
    // 것으로 달성한다.
    class ui_element
    {
    public:
        explicit ui_element(ui_element_id id) noexcept;
        virtual ~ui_element() = default;
        ui_element(const ui_element&) = delete;
        ui_element(ui_element&&) = delete;
        ui_element& operator=(const ui_element&) = delete;
        ui_element& operator=(ui_element&&) = delete;

        [[nodiscard]] const ui_element_id& id() const noexcept;
        [[nodiscard]] const rect_f& bounds() const noexcept;
        [[nodiscard]] bool enabled() const noexcept;
        [[nodiscard]] bool visible() const noexcept;
        [[nodiscard]] const std::u8string& tooltip() const noexcept;
        [[nodiscard]] const ui_action* action(ui_trigger trigger) const noexcept;
        [[nodiscard]] const drag_source* drag() const noexcept;
        [[nodiscard]] const drop_target* drop() const noexcept;
        [[nodiscard]] const pointer_drag_target* pointer_drag() const noexcept;
        // 액션·drag·drop·tooltip 중 하나라도 있으면 hit test의 대상이 된다. 비활성
        // element도 tooltip 표시를 위해 hit는 되고 액션 실행만 막는다.
        [[nodiscard]] bool interactive() const noexcept;
        [[nodiscard]] std::span<const std::unique_ptr<ui_element>> children() const noexcept;

        // 빌드 시 구성이다. tree 게시 후에는 호출하지 않는다.
        void set_bounds(const rect_f& bounds) noexcept;
        void set_enabled(bool value) noexcept;
        void set_visible(bool value) noexcept;
        void set_tooltip(std::u8string text);
        void set_action(ui_trigger trigger, ui_action action);
        void clear_action(ui_trigger trigger) noexcept;
        void set_drag_source(std::optional<drag_source> source);
        void set_drop_target(std::optional<drop_target> target);
        void set_pointer_drag_target(std::optional<pointer_drag_target> target);

        // 부모가 준 slot 안에서 자기 bounds와 자식 배치를 확정한다.
        virtual void arrange(const arrange_context& context) = 0;
        // 자신과 자식을 그린다. hover·눌림 강조는 interaction으로 판정한다.
        virtual void draw(draw_context& context, const interaction_snapshot& interaction) const = 0;
        // 좌표를 포함하는 가장 안쪽의 상호작용 대상을 돌려준다. 기본 구현은 자식을
        // 역순(위에 그려진 것 먼저)으로 탐색한 뒤 자기 bounds를 검사한다.
        [[nodiscard]] virtual const ui_element* hit_test(float x, float y) const;

    protected:
        void add_child(std::unique_ptr<ui_element> child);
        void draw_children(draw_context& context, const interaction_snapshot& interaction) const;

    private:
        ui_element_id id_ {};
        rect_f bounds_ {};
        bool enabled_ { true };
        bool visible_ { true };
        std::u8string tooltip_ {};
        std::array<ui_action, ui_trigger_count> actions_ {};
        std::optional<drag_source> drag_source_ {};
        std::optional<drop_target> drop_target_ {};
        std::optional<pointer_drag_target> pointer_drag_target_ {};
        std::vector<std::unique_ptr<ui_element>> children_ {};
    };
} // namespace gitman::ui
