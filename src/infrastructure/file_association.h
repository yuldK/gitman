#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // `.version-list` Windows file association의 ProgID다 (REQ-016, stage-8-plan
    // 5.4). HKCU 범위만 사용하며 등록·제거·판정이 모두 이 값을 기준으로 한다.
    inline constexpr std::u8string_view file_association_prog_id { u8"Gitman.VersionList" };

    // registry subkey의 기본값 하나다. 이름 있는 값은 쓰지 않는다.
    struct registry_default_value
    {
        std::u8string subkey {};
        std::u8string data {};

        [[nodiscard]] bool operator==(const registry_default_value&) const = default;
    };

    // 확장자 연결 subkey 이름이다 (".version-list").
    [[nodiscard]] std::u8string file_association_extension_subkey();

    // 등록 시 `HKCU\Software\Classes` 아래에 쓸 기본값 목록이다. 실행 파일 경로는
    // 절대 경로여야 하며 공백 경로 대비 따옴표로 감싼다. 같은 값을 다시 쓰면
    // 결과가 같으므로 등록은 멱등이다.
    [[nodiscard]] std::vector<registry_default_value> make_file_association_values(std::u8string_view executable_path);

    // 제거 시 지울 ProgID subkey 목록이다. 자식을 먼저 지울 수 있게 깊은 키가
    // 앞이다. 확장자 subkey는 소유 판정을 거쳐 별도로 처리한다.
    [[nodiscard]] std::vector<std::u8string> file_association_prog_id_subkeys();

    // 확장자 키의 현재 기본값이 우리 ProgID일 때만 연결을 지운다. 다른 앱이
    // 가져간 연결은 건드리지 않는다 (stage-8-plan 5.4).
    [[nodiscard]] bool owns_extension_link(std::u8string_view current_prog_id) noexcept;
} // namespace gitman
