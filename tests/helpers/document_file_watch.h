#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace gitman::testing {
    // 문서 파일을 열지 않고 본 크기와 마지막으로 쓴 시각이다.
    // store의 저장은 임시 파일을 문서 위로 rename해 바꾸는데(`MoveFileExW` + `MOVEFILE_REPLACE_EXISTING`), 그 순간 문서에
    // 열린 handle이 하나라도 있으면 공유 모드와 상관없이 교체가 `ERROR_ACCESS_DENIED`로 실패하고 store는 다시 하지 않는다.
    // 그래서 저장을 기다리는 test는 문서를 읽으며 기다리면 안 되고, 디렉터리 항목만 보고 교체를 확인한 뒤 한 번 읽는다
    // (function-buttons-design "구현 결과" S5).
    struct document_file_stamp
    {
        bool exists { false };
        std::uint64_t size { 0 };
        std::uint64_t last_write { 0 };

        friend bool operator==(const document_file_stamp&, const document_file_stamp&) = default;
    };

    // 문서가 든 디렉터리를 찾아 항목의 크기·시각을 읽는다. 문서 자체에는 handle을 열지 않는다.
    [[nodiscard]] document_file_stamp stamp_document_file(const std::u8string& path);

    // 문서의 항목이 `before`와 달라질 때까지 기다린다. 교체를 본 뒤에야 문서를 읽어도 저장과 겹치지 않는다.
    [[nodiscard]] bool wait_for_document_replacement(const std::u8string& path, const document_file_stamp& before, std::chrono::milliseconds limit);

    // 교체를 볼 때마다 문서를 한 번 읽어 `accept`가 받아들이면 그 내용을 돌려준다. 받아들이지 않으면 그때의 항목을 새 기준으로
    // 삼아 마감까지 다음 교체를 기다린다. 다른 저장이 먼저 교체했거나 기다리던 저장이 두 번에 걸쳐 끝나도 놓치지 않고,
    // 읽는 것은 교체를 본 직후 한 번뿐이라 저장과 겹칠 틈을 늘리지 않는다.
    [[nodiscard]] std::optional<std::string> wait_for_document_content(
        const std::u8string& path
        , const document_file_stamp& before
        , std::chrono::milliseconds limit
        , const std::function<bool(const std::string&)>& accept
    );
}
