#include "application/svn_repository_browser.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        svn_repository_browser_node* find_node(svn_repository_browser_state& state, const std::u8string_view url) noexcept
        {
            const auto found { state.nodes.find(url) };
            return found == state.nodes.end() ? nullptr : &found->second;
        }

        bool is_ascii_unreserved(const char8_t value) noexcept
        {
            return (value >= u8'a' && value <= u8'z') || (value >= u8'A' && value <= u8'Z') || (value >= u8'0' && value <= u8'9') || value == u8'-' || value == u8'.' || value == u8'_'
                || value == u8'~';
        }

        char8_t hexadecimal_digit(const unsigned int value) noexcept
        {
            return static_cast<char8_t>(value < 10 ? u8'0' + value : u8'A' + value - 10);
        }

        char8_t uppercase_hexadecimal(const char8_t value) noexcept
        {
            return value >= u8'a' && value <= u8'f' ? static_cast<char8_t>(value - u8'a' + u8'A') : value;
        }

        // root부터 `reveal_url`까지 훑어 내려가며 cache된 노드를 펼친다. 조회가
        // 필요한 첫 노드를 만나면 그 URL을 돌려주고 멈춘다 — 다음 걸음은 그 조회의
        // `complete_svn_browser_query`가 잇는다. depth만큼만 도는 값싼 순회라 매번
        // root부터 다시 시작해도 된다.
        svn_browser_reveal_progress descend_toward_reveal(svn_repository_browser_state& state)
        {
            svn_browser_reveal_progress progress {};
            if (state.reveal_url.empty())
                return progress;

            const std::u8string target { state.reveal_url };
            if (svn_browser_url_contains(state.root_url, target) == false)
            {
                state.reveal_url.clear();
                progress.not_found = true;
                return progress;
            }

            svn_repository_browser_node* node { find_node(state, state.root_url) };
            while (node != nullptr)
            {
                if (node->url == target)
                {
                    // 목표 자신은 펼치지 않는다. 그 아래는 사용자가 열 때 조회한다.
                    state.selected_url = node->url;
                    state.reveal_url.clear();
                    progress.reached = true;
                    return progress;
                }

                node->expanded = true;
                if (node->load_state == svn_browser_node_load_state::not_loaded || node->load_state == svn_browser_node_load_state::failed)
                {
                    node->load_state = svn_browser_node_load_state::loading;
                    node->error_message.clear();
                    progress.query_url = node->url;
                    return progress;
                }
                // 이미 날아간 조회의 결과가 이어서 내려간다.
                if (node->load_state == svn_browser_node_load_state::loading)
                    return progress;

                svn_repository_browser_node* next { nullptr };
                for (const std::u8string& child_url : node->child_urls)
                    if (svn_browser_url_contains(child_url, target))
                    {
                        next = find_node(state, child_url);
                        break;
                    }
                if (next == nullptr)
                {
                    // 조회한 목록에 그 조각이 없다. 없는 경로다.
                    state.reveal_url.clear();
                    progress.not_found = true;
                    return progress;
                }
                node = next;
            }

            state.reveal_url.clear();
            progress.not_found = true;
            return progress;
        }

        void append_visible_rows(const svn_repository_browser_state& state, const svn_repository_browser_node& node, const std::size_t depth, std::vector<svn_repository_browser_row>& rows,
            std::vector<std::u8string>& ancestors)
        {
            // 상태는 이 모듈만 만들지만, 손상된 입력에서도 재귀 순환을 막는다.
            if (std::ranges::find(ancestors, node.url) != ancestors.end())
                return;

            svn_repository_browser_row row {};
            row.kind = svn_browser_row_kind::directory;
            row.url = node.url;
            row.text = node.name;
            row.depth = depth;
            row.expanded = node.expanded;
            row.can_expand = node.load_state != svn_browser_node_load_state::loaded || node.child_urls.empty() == false;
            row.current = node.url == state.current_url;
            row.selected = node.url == state.selected_url;
            rows.push_back(std::move(row));

            if (node.expanded == false)
                return;

            if (node.load_state == svn_browser_node_load_state::loading)
            {
                rows.push_back({ svn_browser_row_kind::loading, node.url, std::u8string { u8"조회 중…" }, depth + 1 });
                return;
            }
            if (node.load_state == svn_browser_node_load_state::failed)
            {
                rows.push_back({ svn_browser_row_kind::error, node.url, node.error_message, depth + 1 });
                return;
            }
            if (node.load_state != svn_browser_node_load_state::loaded)
                return;

            ancestors.push_back(node.url);
            for (const std::u8string& child_url : node.child_urls)
                if (const svn_repository_browser_node* const child { find_svn_browser_node(state, child_url) }; child != nullptr)
                    append_visible_rows(state, *child, depth + 1, rows, ancestors);
            ancestors.pop_back();
        }
    } // namespace

    bool svn_directory_query_result::succeeded() const noexcept
    {
        return error == svn_browser_query_error::none;
    }

    std::u8string normalize_svn_browser_url(const std::u8string_view url)
    {
        std::u8string normalized { url };
        const std::size_t scheme_separator { normalized.find(u8"://") };
        const std::size_t minimum_size { normalized.starts_with(u8"file:///") ? 8u : (scheme_separator == std::u8string::npos ? 0u : scheme_separator + 3u) };
        while (normalized.size() > minimum_size && normalized.ends_with(u8'/'))
            normalized.pop_back();
        for (std::size_t index = 0; index + 2 < normalized.size(); ++index)
            if (normalized[index] == u8'%')
            {
                normalized[index + 1] = uppercase_hexadecimal(normalized[index + 1]);
                normalized[index + 2] = uppercase_hexadecimal(normalized[index + 2]);
                index += 2;
            }
        return normalized;
    }

    std::u8string append_svn_browser_url(const std::u8string_view parent_url, const std::u8string_view directory_name)
    {
        if (directory_name.empty() || directory_name == u8"." || directory_name == u8"..")
            return {};
        for (const char8_t value : directory_name)
            if (value < u8' ' || value == u8'/')
                return {};

        std::u8string result { normalize_svn_browser_url(parent_url) };
        if (result.empty())
            return {};
        if (result.ends_with(u8'/') == false)
            result.push_back(u8'/');
        for (const char8_t value : directory_name)
        {
            if (is_ascii_unreserved(value))
            {
                result.push_back(value);
                continue;
            }
            const unsigned int byte { static_cast<unsigned char>(value) };
            result.push_back(u8'%');
            result.push_back(hexadecimal_digit(byte >> 4));
            result.push_back(hexadecimal_digit(byte & 0x0Fu));
        }
        return result;
    }

    bool svn_browser_url_contains(const std::u8string_view parent_url, const std::u8string_view candidate_url) noexcept
    {
        if (parent_url == candidate_url)
            return true;
        if (parent_url.ends_with(u8'/'))
            return candidate_url.size() > parent_url.size() && candidate_url.starts_with(parent_url);
        return candidate_url.size() > parent_url.size() && candidate_url.starts_with(parent_url) && candidate_url[parent_url.size()] == u8'/';
    }

    std::u8string_view svn_browser_query_error_message(const svn_browser_query_error error) noexcept
    {
        switch (error)
        {
        case svn_browser_query_error::none:
            return u8"";
        case svn_browser_query_error::authentication_required:
            return u8"인증이 필요해 조회하지 못했습니다";
        case svn_browser_query_error::offline:
            return u8"원격 저장소에 연결하지 못했습니다";
        case svn_browser_query_error::repository_not_found:
            return u8"디렉터리를 찾을 수 없습니다";
        case svn_browser_query_error::timed_out:
            return u8"조회 제한 시간을 초과했습니다";
        case svn_browser_query_error::cancelled:
            return u8"조회가 취소되었습니다";
        case svn_browser_query_error::failed:
            return u8"디렉터리를 조회하지 못했습니다";
        }
        return u8"디렉터리를 조회하지 못했습니다";
    }

    svn_repository_browser_state make_svn_repository_browser(const svn_repository_browser_info& info)
    {
        svn_repository_browser_state state {};
        state.root_url = normalize_svn_browser_url(info.repository_root_url);
        state.current_url = normalize_svn_browser_url(info.current_url);
        state.selected_url = state.current_url;
        if (state.root_url.empty())
            return state;

        svn_repository_browser_node root {};
        root.name = state.root_url;
        root.url = state.root_url;
        root.expanded = true;
        std::u8string root_key { root.url };
        state.nodes.emplace(std::move(root_key), std::move(root));
        return state;
    }

    const svn_repository_browser_node* find_svn_browser_node(const svn_repository_browser_state& state, const std::u8string_view url) noexcept
    {
        const auto found { state.nodes.find(url) };
        return found == state.nodes.end() ? nullptr : &found->second;
    }

    bool begin_svn_browser_query(svn_repository_browser_state& state, const std::u8string_view url)
    {
        svn_repository_browser_node* const node { find_node(state, url) };
        if (node == nullptr || node->load_state == svn_browser_node_load_state::loading || node->load_state == svn_browser_node_load_state::loaded)
            return false;
        node->expanded = true;
        node->load_state = svn_browser_node_load_state::loading;
        node->error_message.clear();
        return true;
    }

    std::optional<std::u8string> toggle_svn_browser_node(svn_repository_browser_state& state, const std::u8string_view url)
    {
        svn_repository_browser_node* const node { find_node(state, url) };
        if (node == nullptr)
            return std::nullopt;
        if (node->load_state == svn_browser_node_load_state::loaded && node->child_urls.empty())
            return std::nullopt;

        if (node->expanded)
        {
            node->expanded = false;
            return std::nullopt;
        }

        node->expanded = true;
        if (node->load_state == svn_browser_node_load_state::not_loaded || node->load_state == svn_browser_node_load_state::failed)
        {
            node->load_state = svn_browser_node_load_state::loading;
            node->error_message.clear();
            return node->url;
        }
        return std::nullopt;
    }

    svn_browser_reveal_progress complete_svn_browser_query(svn_repository_browser_state& state, const std::u8string_view url, const svn_directory_query_result& result)
    {
        svn_repository_browser_node* node { find_node(state, url) };
        if (node == nullptr || node->load_state != svn_browser_node_load_state::loading)
            return {};

        node->child_urls.clear();
        if (result.succeeded() == false)
        {
            node->load_state = svn_browser_node_load_state::failed;
            node->error_message = svn_browser_query_error_message(result.error);
            // 목표로 가는 길이 끊겼다. 실패는 그 노드의 오류 행이 알린다.
            if (state.reveal_url.empty() == false && svn_browser_url_contains(node->url, state.reveal_url))
                state.reveal_url.clear();
            return {};
        }

        // 같은 이름이나 해석할 수 없는 이름은 cache에 넣지 않는다. URL key map이
        // lookup을 맡아 큰 디렉터리에서도 전체 node 선형 검색을 반복하지 않는다.
        const std::u8string parent_url { node->url };
        node->load_state = svn_browser_node_load_state::loaded;
        for (const std::u8string& name : result.directories)
        {
            std::u8string child_url { append_svn_browser_url(parent_url, name) };
            if (child_url.empty())
                continue;
            svn_repository_browser_node* parent { find_node(state, parent_url) };
            if (parent == nullptr || std::ranges::find(parent->child_urls, child_url) != parent->child_urls.end())
                continue;

            if (find_node(state, child_url) == nullptr)
            {
                svn_repository_browser_node child {};
                child.name = name;
                child.url = child_url;
                child.parent_url = parent_url;
                std::u8string child_key { child.url };
                state.nodes.emplace(std::move(child_key), std::move(child));
            }
            if (parent != nullptr)
                parent->child_urls.push_back(std::move(child_url));
        }
        return descend_toward_reveal(state);
    }

    svn_browser_reveal_progress begin_svn_browser_reveal(svn_repository_browser_state& state, const std::u8string_view url)
    {
        state.reveal_url = normalize_svn_browser_url(url);
        if (state.reveal_url.empty())
        {
            svn_browser_reveal_progress progress {};
            progress.not_found = true;
            return progress;
        }
        return descend_toward_reveal(state);
    }

    bool reset_svn_browser_node(svn_repository_browser_state& state, const std::u8string_view url)
    {
        svn_repository_browser_node* const node { find_node(state, url) };
        if (node == nullptr)
            return false;

        // map은 지운 원소 밖의 참조를 무효로 만들지 않으므로 `node`는 계속 쓸 수 있다.
        const std::u8string target { node->url };
        for (auto entry = state.nodes.begin(); entry != state.nodes.end();)
            if (entry->first != target && svn_browser_url_contains(target, entry->first))
                entry = state.nodes.erase(entry);
            else
                ++entry;

        node->child_urls.clear();
        node->load_state = svn_browser_node_load_state::not_loaded;
        node->error_message.clear();
        node->expanded = true;
        // 사라진 하위를 가리키던 선택과 목표는 이 노드로 되돌린다.
        if (state.selected_url != target && svn_browser_url_contains(target, state.selected_url))
            state.selected_url = target;
        state.reveal_url.clear();
        return true;
    }

    std::u8string svn_browser_relative_path(const std::u8string_view root_url, const std::u8string_view url)
    {
        const std::u8string root { normalize_svn_browser_url(root_url) };
        const std::u8string target { normalize_svn_browser_url(url) };
        if (root.empty() || svn_browser_url_contains(root, target) == false)
            return {};
        if (root == target)
            return std::u8string { u8"/" };
        // root가 `file:///`처럼 `/`로 끝나면 구분자가 이미 포함되어 있다.
        const std::size_t offset { root.ends_with(u8'/') ? root.size() - 1u : root.size() };
        return target.substr(offset);
    }

    std::u8string resolve_svn_browser_path(const svn_repository_browser_state& state, const std::u8string_view text)
    {
        std::u8string_view trimmed { text };
        while (trimmed.empty() == false && (trimmed.front() == u8' ' || trimmed.front() == u8'\t'))
            trimmed.remove_prefix(1);
        while (trimmed.empty() == false && (trimmed.back() == u8' ' || trimmed.back() == u8'\t'))
            trimmed.remove_suffix(1);
        if (trimmed.empty() || state.root_url.empty())
            return {};

        // 이미 인코딩된 전체 URL은 다시 인코딩하지 않는다.
        if (svn_browser_url_contains(state.root_url, normalize_svn_browser_url(trimmed)))
            return normalize_svn_browser_url(trimmed);

        while (trimmed.empty() == false && trimmed.front() == u8'/')
            trimmed.remove_prefix(1);
        std::u8string result { state.root_url };
        while (trimmed.empty() == false)
        {
            const std::size_t separator { trimmed.find(u8'/') };
            const std::u8string_view segment { trimmed.substr(0, separator) };
            trimmed = separator == std::u8string_view::npos ? std::u8string_view {} : trimmed.substr(separator + 1);
            // 끝의 `/`나 이어진 `//`는 빈 조각이라 건너뛴다.
            if (segment.empty())
                continue;
            result = append_svn_browser_url(result, segment);
            if (result.empty())
                return {};
        }
        return result;
    }

    bool select_svn_browser_node(svn_repository_browser_state& state, const std::u8string_view url)
    {
        if (find_svn_browser_node(state, url) == nullptr)
            return false;
        state.selected_url = url;
        return true;
    }

    std::vector<svn_repository_browser_row> build_svn_repository_browser_rows(const svn_repository_browser_state& state)
    {
        std::vector<svn_repository_browser_row> rows {};
        const svn_repository_browser_node* const root { find_svn_browser_node(state, state.root_url) };
        if (root == nullptr)
            return rows;
        std::vector<std::u8string> ancestors {};
        append_visible_rows(state, *root, 0, rows, ancestors);
        return rows;
    }
} // namespace gitman
