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

        std::optional<std::u8string> advance_current_path(svn_repository_browser_state& state, const std::u8string_view loaded_url)
        {
            svn_repository_browser_node* parent { find_node(state, loaded_url) };
            while (parent != nullptr && parent->load_state == svn_browser_node_load_state::loaded && parent->url != state.current_url && svn_browser_url_contains(parent->url, state.current_url))
            {
                svn_repository_browser_node* next { nullptr };
                for (const std::u8string& child_url : parent->child_urls)
                {
                    if (svn_browser_url_contains(child_url, state.current_url))
                    {
                        next = find_node(state, child_url);
                        break;
                    }
                }
                if (next == nullptr)
                    return std::nullopt;

                // 현재 위치 자체는 보이기만 하면 된다. 그 아래까지 조회하지 않는다.
                if (next->url == state.current_url)
                    return std::nullopt;

                next->expanded = true;
                if (next->load_state == svn_browser_node_load_state::not_loaded || next->load_state == svn_browser_node_load_state::failed)
                {
                    next->load_state = svn_browser_node_load_state::loading;
                    next->error_message.clear();
                    return next->url;
                }
                if (next->load_state == svn_browser_node_load_state::loading)
                    return std::nullopt;
                parent = next;
            }
            return std::nullopt;
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

    std::optional<std::u8string> complete_svn_browser_query(svn_repository_browser_state& state, const std::u8string_view url, const svn_directory_query_result& result)
    {
        svn_repository_browser_node* node { find_node(state, url) };
        if (node == nullptr || node->load_state != svn_browser_node_load_state::loading)
            return std::nullopt;

        node->child_urls.clear();
        if (result.succeeded() == false)
        {
            node->load_state = svn_browser_node_load_state::failed;
            node->error_message = svn_browser_query_error_message(result.error);
            return std::nullopt;
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
        return advance_current_path(state, parent_url);
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
