#pragma once

#include "application/application_options.h"

#include <windows.h>

namespace gitman::win32 {
    [[nodiscard]] int run_application(HINSTANCE instance, const application_options& options);
} // namespace gitman::win32
