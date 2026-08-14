#include "platform/win32/skia_renderer.h"

#include <utility>

namespace gitman::win32 {
    std::unique_ptr<renderer_host> renderer_host::create(const HWND window, const renderer_mode mode, const bool simulate_direct3d_failure, std::u8string& error)
    {
        if (mode == renderer_mode::cpu)
        {
            renderer_factory_result cpu_result { create_cpu_skia_renderer(window) };
            if (cpu_result.renderer == nullptr)
            {
                error = std::move(cpu_result.error);
                return nullptr;
            }

            return std::unique_ptr<renderer_host>(new renderer_host { window, mode, std::move(cpu_result.renderer), false });
        }

        renderer_factory_result direct3d_result {};
        if (simulate_direct3d_failure)
            direct3d_result.error = u8"The smoke test injected a Direct3D initialization failure.";
        else
            direct3d_result = create_direct3d_skia_renderer(window);

        if (direct3d_result.renderer != nullptr)
            return std::unique_ptr<renderer_host>(new renderer_host { window, mode, std::move(direct3d_result.renderer), false });
        if (mode == renderer_mode::direct3d)
        {
            error = std::move(direct3d_result.error);
            return nullptr;
        }

        renderer_factory_result cpu_result { create_cpu_skia_renderer(window) };
        if (cpu_result.renderer == nullptr)
        {
            error = u8"Both Direct3D and CPU renderer initialization failed. ";
            error += cpu_result.error;
            return nullptr;
        }

        return std::unique_ptr<renderer_host>(new renderer_host { window, mode, std::move(cpu_result.renderer), true });
    }

    renderer_host::renderer_host(const HWND window, const renderer_mode mode, std::unique_ptr<skia_renderer> renderer, const bool used_fallback) noexcept
        : window_ { window }
        , mode_ { mode }
        , renderer_ { std::move(renderer) }
        , used_fallback_ { used_fallback }
    {}

    renderer_backend renderer_host::backend() const noexcept
    {
        return renderer_->backend();
    }

    bool renderer_host::used_fallback() const noexcept
    {
        return used_fallback_;
    }

    bool renderer_host::resize(const int width, const int height, std::u8string& error)
    {
        width_ = width;
        height_ = height;
        if (renderer_->resize(width, height, error))
            return true;
        if (mode_ != renderer_mode::automatic || renderer_->backend() != renderer_backend::direct3d)
            return false;
        if (switch_to_cpu(error) == false)
            return false;
        return renderer_->resize(width, height, error);
    }

    bool renderer_host::render(smoke_view_state state, std::u8string& error)
    {
        state.backend = renderer_->backend();
        state.used_fallback = used_fallback_;

        if (renderer_->render(state, error))
            return true;

        if (mode_ != renderer_mode::automatic || renderer_->backend() != renderer_backend::direct3d)
            return false;

        if (switch_to_cpu(error) == false)
            return false;

        state.backend = renderer_->backend();
        state.used_fallback = true;
        return renderer_->render(state, error);
    }

    bool renderer_host::switch_to_cpu(std::u8string& error)
    {
        renderer_factory_result cpu_result { create_cpu_skia_renderer(window_) };
        if (cpu_result.renderer == nullptr)
        {
            error += u8" CPU fallback initialization also failed: ";
            error += cpu_result.error;
            return false;
        }
        renderer_ = std::move(cpu_result.renderer);
        used_fallback_ = true;
        return true;
    }
} // namespace gitman::win32
