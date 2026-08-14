#pragma once

#include "presentation/renderer_policy.h"
#include "presentation/skia_smoke_view.h"

#include <windows.h>

#include <memory>
#include <string>

namespace gitman::win32 {
    class skia_renderer
    {
    public:
        virtual ~skia_renderer() = default;

        [[nodiscard]] virtual renderer_backend backend() const noexcept = 0;
        [[nodiscard]] virtual bool resize(int width, int height, std::u8string& error) = 0;
        [[nodiscard]] virtual bool render(const smoke_view_state& state, std::u8string& error) = 0;
    };

    struct renderer_factory_result
    {
        std::unique_ptr<skia_renderer> renderer {};
        std::u8string error {};
    };

    class renderer_host
    {
    public:
        static std::unique_ptr<renderer_host> create(HWND window, renderer_mode mode, bool simulate_direct3d_failure, std::u8string& error);

        [[nodiscard]] renderer_backend backend() const noexcept;
        [[nodiscard]] bool used_fallback() const noexcept;
        [[nodiscard]] bool resize(int width, int height, std::u8string& error);
        [[nodiscard]] bool render(smoke_view_state state, std::u8string& error);

    private:
        renderer_host(HWND window, renderer_mode mode, std::unique_ptr<skia_renderer> renderer, bool used_fallback) noexcept;

        [[nodiscard]] bool switch_to_cpu(std::u8string& error);

        HWND window_ { nullptr };
        renderer_mode mode_ { renderer_mode::automatic };
        std::unique_ptr<skia_renderer> renderer_ {};
        bool used_fallback_ { false };
        int width_ { 1 };
        int height_ { 1 };
    };

    [[nodiscard]] renderer_factory_result create_cpu_skia_renderer(HWND window);
    [[nodiscard]] renderer_factory_result create_direct3d_skia_renderer(HWND window);
} // namespace gitman::win32
