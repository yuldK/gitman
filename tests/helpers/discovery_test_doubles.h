#pragma once

#include "application/directory_enumerator.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gitman::testing {
    // 실제 filesystem 탐색 test 전용 임시 디렉터리다. 실패한 test에서도 남지 않도록
    // 소멸자에서 지운다.
    class scoped_scan_directory
    {
    public:
        scoped_scan_directory();
        scoped_scan_directory(const scoped_scan_directory&) = delete;
        scoped_scan_directory(scoped_scan_directory&&) = delete;
        scoped_scan_directory& operator=(const scoped_scan_directory&) = delete;
        scoped_scan_directory& operator=(scoped_scan_directory&&) = delete;
        ~scoped_scan_directory();

        [[nodiscard]] bool available() const noexcept;
        [[nodiscard]] std::u8string root() const;
        [[nodiscard]] std::u8string path_of(std::u8string_view relative) const;
        [[nodiscard]] std::u8string make_directory(std::u8string_view relative) const;
        [[nodiscard]] std::u8string make_file(std::u8string_view relative) const;

    private:
        std::filesystem::path root_ {};
    };

    // 등록한 디렉터리만 열거되는 filesystem 대역이다. Windows 경로 비교와 같게 ASCII
    // 대소문자를 무시하고 구분자도 동일하게 취급한다. 등록하지 않은 경로는 경로 없음
    // 오류로 실패해 탐색의 실패 경로를 결정적으로 만들 수 있다.
    class fake_directory_enumerator final : public directory_enumerator
    {
    public:
        void set_listing(std::u8string_view absolute_directory, directory_listing listing);

        // 탐색이 열거를 몇 번 요청했는지 확인한다. 취소 뒤에 열거가 이어지지 않는
        // 것을 이 수로 단정한다.
        [[nodiscard]] std::size_t enumeration_count() const noexcept;

        [[nodiscard]] directory_listing enumerate(std::u8string_view absolute_directory) const noexcept override;

    private:
        struct entry
        {
            std::u8string path {};
            directory_listing listing {};
        };

        std::vector<entry> entries_ {};
        mutable std::size_t enumeration_count_ { 0 };
    };
} // namespace gitman::testing
