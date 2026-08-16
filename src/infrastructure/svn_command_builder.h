#pragma once

#include "application/process_request.h"

#include <string_view>

namespace gitman {
    // `svn info --show-item`으로 값 하나만 받아 올 수 있는 항목이다. 사람이 읽는
    // `key: value` 목록을 파싱하지 않으므로 로캘과 무관하고 파서 위험이 가장 작다.
    enum class svn_info_item
    {
        url,
        relative_url,
        repository_root,
        repository_uuid,
        revision,
        working_copy_root,
    };

    [[nodiscard]] std::u8string_view svn_info_item_name(svn_info_item item) noexcept;

    // `target`이 비어 있으면 작업 디렉터리를 대상으로 삼는다. URL을 주면 원격 조회다.
    [[nodiscard]] process_request make_svn_info_item_request(std::u8string_view executable, std::u8string_view working_directory, svn_info_item item, std::u8string_view target = {});

    // 비verbose `status`다. 앞의 상태 칸이 고정 폭이고 그 뒤가 전부 경로라 공백이 든
    // 경로에서도 경계가 모호하지 않다. `--verbose`는 작성자 컬럼 때문에 쓰지 않는다.
    [[nodiscard]] process_request make_svn_status_request(std::u8string_view executable, std::u8string_view working_directory);

    // `svnversion`은 `svn`과 다른 실행 파일이며 `--non-interactive`를 받지 않는다.
    // 따라서 SVN 공통 인자를 붙이지 않고 요청을 직접 만든다.
    [[nodiscard]] process_request make_svnversion_request(std::u8string_view executable, std::u8string_view working_directory);

    // 원격 HEAD 리비전이다. 네트워크를 쓰는 유일한 조회다.
    [[nodiscard]] process_request make_svn_remote_revision_request(std::u8string_view executable, std::u8string_view working_directory, std::u8string_view url);

    // 등록된 작업 복사본 루트에서 그대로 실행한다. `--accept`를 주지 않으므로 충돌은
    // 자동으로 해결되지 않고 그대로 남는다.
    [[nodiscard]] process_request make_svn_update_request(std::u8string_view executable, std::u8string_view working_directory);
} // namespace gitman
