#include "domain/local_changes.h"

namespace gitman {
    std::u8string_view local_change_kind_badge(const local_change_kind kind) noexcept
    {
        switch (kind)
        {
        case local_change_kind::modified:
            return u8"수정";
        case local_change_kind::added:
            return u8"추가";
        case local_change_kind::deleted:
            return u8"삭제";
        case local_change_kind::renamed:
            return u8"이동";
        case local_change_kind::conflicted:
            return u8"충돌";
        case local_change_kind::untracked:
            return u8"미추적";
        case local_change_kind::other:
            break;
        }
        return u8"변경";
    }

    std::u8string join_local_change_path(const std::u8string_view working_directory, const std::u8string_view relative_path)
    {
        std::u8string joined { working_directory };
        if (joined.empty() == false && joined.back() != u8'\\' && joined.back() != u8'/')
            joined.push_back(u8'\\');
        joined.append(relative_path);
        while (joined.empty() == false && (joined.back() == u8'/' || joined.back() == u8'\\'))
            joined.pop_back();
        // status의 상대 경로는 `/` 구분자다. explorer `/select,`가 `/` 경로를 받지
        // 못하므로 Windows 구분자로 통일한다.
        for (char8_t& value : joined)
            if (value == u8'/')
                value = u8'\\';
        return joined;
    }
} // namespace gitman
