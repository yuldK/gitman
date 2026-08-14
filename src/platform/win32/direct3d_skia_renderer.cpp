#include "platform/win32/skia_renderer.h"

#include "platform/win32/embedded_assets.h"

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>

namespace gitman::win32 {
    namespace {
        constexpr std::size_t frame_count { 2 };

        std::u8string make_hresult_error(const std::u8string_view message, const HRESULT result)
        {
            std::u8string output { message };
            output += u8" (HRESULT=0x";
            std::array<char, 16> buffer {};
            const auto conversion {
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<unsigned long>(result), 16),
            };

            if (conversion.ec == std::errc {})
                for (const char* character = buffer.data(); character != conversion.ptr; ++character)
                    output.push_back(static_cast<char8_t>(*character));
            output += u8")";
            return output;
        }

        gr_cp<IDXGIAdapter1> find_hardware_adapter(IDXGIFactory4& factory)
        {
            for (UINT adapter_index { 0 };; ++adapter_index)
            {
                IDXGIAdapter1* raw_adapter { nullptr };
                if (factory.EnumAdapters1(adapter_index, &raw_adapter) == DXGI_ERROR_NOT_FOUND)
                    break;

                gr_cp<IDXGIAdapter1> adapter { raw_adapter };
                DXGI_ADAPTER_DESC1 description {};
                if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                    continue;
                if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
                    return adapter;
            }
            return nullptr;
        }

        class direct3d_skia_renderer final : public skia_renderer
        {
        public:
            explicit direct3d_skia_renderer(const HWND window)
                : window_(window)
                , codicon_typeface_(load_codicon_typeface())
                , ui_typeface_(load_ui_typeface())
            {}

            ~direct3d_skia_renderer() override
            {
                static_cast<void>(wait_for_gpu());
                release_surfaces();
                if (context_ != nullptr)
                    context_->releaseResourcesAndAbandonContext();
                context_.reset();
                if (fence_event_ != nullptr)
                    CloseHandle(fence_event_);
            }

            [[nodiscard]] bool initialize(std::u8string& error)
            {
                HRESULT result { CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)) };
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the DXGI factory", result);
                    return false;
                }

                adapter_ = find_hardware_adapter(*factory_.get());
                if (!adapter_)
                {
                    error = u8"No hardware adapter supports Direct3D 12.";
                    return false;
                }

                result = D3D12CreateDevice(adapter_.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D 12 device", result);
                    return false;
                }

                D3D12_COMMAND_QUEUE_DESC queue_description {};
                queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                queue_description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                result = device_->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D command queue", result);
                    return false;
                }

                GrD3DBackendContext backend_context {};
                backend_context.fAdapter = adapter_;
                backend_context.fDevice = device_;
                backend_context.fQueue = queue_;
                context_ = GrDirectContexts::MakeD3D(backend_context);
                if (context_ == nullptr)
                {
                    error = u8"Failed to create the Skia Direct3D context.";
                    return false;
                }

                RECT client_rectangle {};
                if (GetClientRect(window_, &client_rectangle) == FALSE)
                {
                    error = u8"The Direct3D renderer failed to read the initial client size.";
                    return false;
                }
                width_ = std::max(1L, client_rectangle.right - client_rectangle.left);
                height_ = std::max(1L, client_rectangle.bottom - client_rectangle.top);

                DXGI_SWAP_CHAIN_DESC1 swap_chain_description {};
                swap_chain_description.BufferCount = static_cast<UINT>(frame_count);
                swap_chain_description.Width = static_cast<UINT>(width_);
                swap_chain_description.Height = static_cast<UINT>(height_);
                swap_chain_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                swap_chain_description.SampleDesc.Count = 1;

                gr_cp<IDXGISwapChain1> initial_swap_chain {};
                result = factory_->CreateSwapChainForHwnd(queue_.get(), window_, &swap_chain_description, nullptr, nullptr, &initial_swap_chain);
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D swapchain", result);
                    return false;
                }
                factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
                result = initial_swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to query IDXGISwapChain3", result);
                    return false;
                }

                result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D fence", result);
                    return false;
                }
                fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (fence_event_ == nullptr)
                {
                    error = u8"Failed to create the Direct3D fence event.";
                    return false;
                }
                return setup_surfaces(error);
            }

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::direct3d;
            }

            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                const int safe_width { std::max(1, width) };
                const int safe_height { std::max(1, height) };
                if (safe_width == width_ && safe_height == height_)
                    return true;
                if (wait_for_gpu() == false)
                {
                    error = u8"Failed to wait for the GPU before resizing Direct3D resources.";
                    return false;
                }

                context_->flush();
                context_->submit(GrSyncCpu::kYes);
                release_surfaces();
                const HRESULT result {
                    swap_chain_->ResizeBuffers(static_cast<UINT>(frame_count), static_cast<UINT>(safe_width), static_cast<UINT>(safe_height), DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                };

                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to resize the Direct3D swapchain", result);
                    return false;
                }
                width_ = safe_width;
                height_ = safe_height;
                frame_fence_values_.fill(0);
                return setup_surfaces(error);
            }

            [[nodiscard]] bool render(const smoke_view_state& state, std::u8string& error) override
            {
                const UINT frame_index { swap_chain_->GetCurrentBackBufferIndex() };
                if (wait_for_fence(frame_fence_values_[frame_index]) == false)
                {
                    error = u8"Failed to wait for the Direct3D back buffer fence.";
                    return false;
                }

                draw_skia_smoke_view(*surfaces_[frame_index]->getCanvas(), codicon_typeface_.get(), ui_typeface_.get(), state);
                GrFlushInfo flush_info {};
                context_->flush(surfaces_[frame_index].get(), SkSurfaces::BackendSurfaceAccess::kPresent, flush_info);
                context_->submit();

                HRESULT result { swap_chain_->Present(1, 0) };
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to present the Direct3D frame", result);
                    return false;
                }

                const std::uint64_t signal_value { ++next_fence_value_ };
                result = queue_->Signal(fence_.get(), signal_value);
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to signal the Direct3D queue", result);
                    return false;
                }
                frame_fence_values_[frame_index] = signal_value;
                return true;
            }

        private:
            [[nodiscard]] bool setup_surfaces(std::u8string& error)
            {
                GrD3DTextureResourceInfo resource_info {
                    nullptr,
                    nullptr,
                    D3D12_RESOURCE_STATE_PRESENT,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    1,
                    1,
                    0,
                };
                for (std::size_t index = 0; index < frame_count; ++index)
                {
                    HRESULT result {
                        swap_chain_->GetBuffer(static_cast<UINT>(index), IID_PPV_ARGS(&buffers_[index])),
                    };

                    if (FAILED(result))
                    {
                        error = make_hresult_error(u8"Failed to retrieve a Direct3D back buffer", result);
                        return false;
                    }
                    resource_info.fResource = buffers_[index];
                    const GrBackendRenderTarget render_target {
                        GrBackendRenderTargets::MakeD3D(width_, height_, resource_info),
                    };

                    surfaces_[index] = SkSurfaces::WrapBackendRenderTarget(context_.get(), render_target, kTopLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, nullptr, nullptr);
                    if (surfaces_[index] == nullptr)
                    {
                        error = u8"Skia failed to wrap a Direct3D back buffer as a surface.";
                        return false;
                    }
                }
                return true;
            }

            void release_surfaces() noexcept
            {
                for (std::size_t index = 0; index < frame_count; ++index)
                {
                    surfaces_[index].reset();
                    buffers_[index].reset();
                }
            }

            [[nodiscard]] bool wait_for_fence(const std::uint64_t value) noexcept
            {
                if (value == 0 || fence_->GetCompletedValue() >= value)
                    return true;
                if (FAILED(fence_->SetEventOnCompletion(value, fence_event_)))
                    return false;
                return WaitForSingleObject(fence_event_, INFINITE) == WAIT_OBJECT_0;
            }

            [[nodiscard]] bool wait_for_gpu() noexcept
            {
                if (!queue_ || !fence_ || fence_event_ == nullptr)
                    return true;
                const std::uint64_t signal_value { ++next_fence_value_ };
                if (FAILED(queue_->Signal(fence_.get(), signal_value)))
                    return false;
                return wait_for_fence(signal_value);
            }

            HWND window_ { nullptr };
            int width_ { 1 };
            int height_ { 1 };
            gr_cp<IDXGIFactory4> factory_ {};
            gr_cp<IDXGIAdapter1> adapter_ {};
            gr_cp<ID3D12Device> device_ {};
            gr_cp<ID3D12CommandQueue> queue_ {};
            gr_cp<IDXGISwapChain3> swap_chain_ {};
            gr_cp<ID3D12Fence> fence_ {};
            HANDLE fence_event_ { nullptr };
            std::uint64_t next_fence_value_ { 0 };
            std::array<std::uint64_t, frame_count> frame_fence_values_ {};
            std::array<gr_cp<ID3D12Resource>, frame_count> buffers_ {};
            std::array<sk_sp<SkSurface>, frame_count> surfaces_ {};
            sk_sp<GrDirectContext> context_ {};
            sk_sp<SkTypeface> codicon_typeface_ {};
            sk_sp<SkTypeface> ui_typeface_ {};
        };
    } // namespace

    renderer_factory_result create_direct3d_skia_renderer(const HWND window)
    {
        auto renderer { std::make_unique<direct3d_skia_renderer>(window) };
        std::u8string error {};
        if (renderer->initialize(error) == false)
            return { nullptr, std::move(error) };
        return { std::move(renderer), {} };
    }
} // namespace gitman::win32
