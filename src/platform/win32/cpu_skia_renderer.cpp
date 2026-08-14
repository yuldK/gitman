#include "platform/win32/skia_renderer.h"

#include "platform/win32/embedded_assets.h"

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace gitman::win32 {
    namespace {
        class cpu_skia_renderer final : public skia_renderer
        {
        public:
            explicit cpu_skia_renderer(const HWND window)
                : window_ { window }
                , codicon_typeface_ { load_codicon_typeface() }
                , ui_typeface_ { load_ui_typeface() }
            {}

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::cpu;
            }

            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                width_ = std::max(1, width);
                height_ = std::max(1, height);
                const SkImageInfo image_info {
                    SkImageInfo::MakeN32Premul(width_, height_, SkColorSpace::MakeSRGB()),
                };
                surface_ = SkSurfaces::Raster(image_info);
                if (surface_ == nullptr)
                {
                    error = u8"Failed to create the CPU raster surface.";
                    return false;
                }
                return true;
            }

            [[nodiscard]] bool render(const smoke_view_state& state, std::u8string& error) override
            {
                if (surface_ == nullptr && resize(state.width, state.height, error) == false)
                    return false;

                draw_skia_smoke_view(*surface_->getCanvas(), codicon_typeface_.get(), ui_typeface_.get(), state);

                SkPixmap pixels {};
                if (surface_->peekPixels(&pixels) == false)
                {
                    error = u8"Failed to read CPU surface pixels.";
                    return false;
                }

                BITMAPINFO bitmap_info {};
                bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bitmap_info.bmiHeader.biWidth = width_;
                bitmap_info.bmiHeader.biHeight = -height_;
                bitmap_info.bmiHeader.biPlanes = 1;
                bitmap_info.bmiHeader.biBitCount = 32;
                bitmap_info.bmiHeader.biCompression = BI_RGB;

                const HDC device_context { GetDC(window_) };
                if (device_context == nullptr)
                {
                    error = u8"The CPU renderer failed to acquire the window device context.";
                    return false;
                }

                const int copied_lines {
                    StretchDIBits(device_context, 0, 0, width_, height_, 0, 0, width_, height_, pixels.addr(), &bitmap_info, DIB_RGB_COLORS, SRCCOPY),
                };
                ReleaseDC(window_, device_context);
                if (copied_lines == GDI_ERROR || copied_lines == 0)
                {
                    error = u8"The CPU renderer failed to copy the raster frame to the window.";
                    return false;
                }
                return true;
            }

        private:
            HWND window_ { nullptr };
            int width_ { 1 };
            int height_ { 1 };
            sk_sp<SkSurface> surface_ {};
            sk_sp<SkTypeface> codicon_typeface_ {};
            sk_sp<SkTypeface> ui_typeface_ {};
        };
    } // namespace

    renderer_factory_result create_cpu_skia_renderer(const HWND window)
    {
        auto renderer { std::make_unique<cpu_skia_renderer>(window) };
        RECT client_rectangle {};
        if (GetClientRect(window, &client_rectangle) == FALSE)
            return { nullptr, u8"The CPU renderer failed to read the initial client size." };

        std::u8string error {};
        if (false == renderer->resize(client_rectangle.right - client_rectangle.left, client_rectangle.bottom - client_rectangle.top, error))
            return { nullptr, std::move(error) };
        return { std::move(renderer), {} };
    }
} // namespace gitman::win32
