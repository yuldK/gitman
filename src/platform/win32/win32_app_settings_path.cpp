#include "platform/win32/win32_app_settings_path.h"

#include "domain/app_settings.h"
#include "domain/path_syntax.h"
#include "platform/win32/win32_file_association.h"

namespace gitman::win32 {
    std::u8string app_settings_file_path()
    {
        const std::u8string executable { current_executable_path() };
        const std::u8string_view directory { windows_parent_directory(executable) };
        if (directory.empty())
            return {};

        std::u8string path { directory };
        path.push_back(u8'\\');
        path.append(app_settings_file_name);
        return path;
    }
} // namespace gitman::win32
