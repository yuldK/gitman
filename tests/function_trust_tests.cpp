#include "domain/app_settings.h"
#include "domain/function_trust.h"
#include "infrastructure/json_app_settings_store.h"
#include "infrastructure/json_workspace_document.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <variant>

// 기능 버튼 신뢰의 해시와 앱 설정 필드다 (function-buttons-design 3.10, 6.1 T7). logic 흐름(dialog,
// 확인 뒤 다시 치환)은 `logic_function_trust_tests.cpp`에 있다.
namespace {
    constexpr std::u8string_view settings_path { u8"C:\\gitman\\gitman.app-settings.json" };
    constexpr std::u8string_view document_path { u8"E:/repos/team.version-list" };

    // 신뢰 해시 비교용 문서다. 버튼 하나와 project 둘을 둔다.
    struct digest_document
    {
        std::u8string label { u8"빌드" };
        std::u8string icon { u8"tools" };
        std::u8string placement { u8"card" };
        std::u8string program { u8"C:\\\\Tools\\\\build.exe" };
        std::u8string alpha_path { u8"alpha" };
        // 비면 `display_name`을 적지 않는다 (파서가 경로에서 만든다).
        std::u8string alpha_name {};
        bool alpha_enabled { true };
        bool reversed_projects { false };

        [[nodiscard]] std::u8string json() const
        {
            std::u8string source { u8R"({"schema_version":1,"function_buttons":{"variables":{"config":"Debug"},"buttons":[)" };
            source += u8R"({"id":"build","label":")" + label + u8R"(","icon":")" + icon + u8R"(","placement":")" + placement + u8R"(","program":")" + program;
            source += u8R"(","args":["${projectRoot}","${config}"]}]},"projects":[)";
            const std::u8string name { alpha_name.empty() ? std::u8string {} : u8R"(","display_name":")" + alpha_name };
            const std::u8string alpha { u8R"({"id":"alpha","path":")" + alpha_path + name + u8R"(","enabled":)" + (alpha_enabled ? std::u8string { u8"true" } : std::u8string { u8"false" }) + u8"}" };
            const std::u8string beta { u8R"({"id":"beta","path":"beta"})" };
            source += reversed_projects ? beta + u8"," + alpha : alpha + u8"," + beta;
            source += u8"]}";
            return source;
        }

        [[nodiscard]] std::u8string digest() const
        {
            const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(json(), document_path) };
            REQUIRE(result.document.has_value());
            REQUIRE(result.document->function_buttons.has_value());
            REQUIRE(result.document->projects.size() == 2u);
            return gitman::function_trust_digest(*result.document->function_buttons, result.document->projects);
        }
    };
} // namespace

TEST_CASE("SHA-256 matches the FIPS 180-4 examples", "[function_buttons][t7]")
{
    REQUIRE(gitman::sha256_hex(u8"") == u8"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(gitman::sha256_hex(u8"abc") == u8"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 길이 칸이 다음 블록으로 넘어가는 56 byte 입력이다.
    REQUIRE(gitman::sha256_hex(u8"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == u8"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // 블록 둘을 꽉 채우는 112 byte 입력이다.
    REQUIRE(gitman::sha256_hex(u8"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")
        == u8"cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("The trust digest changes with the definition and project paths and names but not with placement", "[function_buttons][t7][r3]")
{
    const digest_document base {};
    const std::u8string digest { base.digest() };
    REQUIRE(digest.size() == 64u);

    // 배치(`placement`)는 해시 입력이 아니다. 재배치는 다시 묻지 않는다 (D29).
    digest_document placed { base };
    placed.placement = u8"bar";
    REQUIRE(placed.digest() == digest);
    placed.placement = u8"menu";
    REQUIRE(placed.digest() == digest);

    // 이름은 신뢰 dialog가 명령과 쌍으로 보인 것이고 아이콘은 `card` 버튼에 보이는 유일한 표시라, 바뀌면 다시 묻는다 (D29).
    digest_document renamed { base };
    renamed.label = u8"빌드 다시";
    REQUIRE(renamed.digest() != digest);
    digest_document iconed { base };
    iconed.icon = u8"rocket";
    REQUIRE(iconed.digest() != digest);

    // 카드 순서 변경과 제외 토글도 해시를 바꾸지 않는다.
    digest_document reordered { base };
    reordered.reversed_projects = true;
    reordered.alpha_enabled = false;
    REQUIRE(reordered.digest() == digest);

    // 정의가 바뀌면 다시 묻는다.
    digest_document program { base };
    program.program = u8"C:\\\\Tools\\\\other.exe";
    REQUIRE(program.digest() != digest);

    // project 경로만 바꿔 `${projectRoot}`가 다른 곳을 가리켜도 다시 묻는다.
    digest_document moved { base };
    moved.alpha_path = u8"elsewhere";
    REQUIRE(moved.digest() != digest);

    // project 표시 이름은 `${projectName}`과 `GITMAN_PROJECT_NAME`으로 실행에 들어가므로 다시 묻는다.
    digest_document named { base };
    named.alpha_name = u8"알파";
    REQUIRE(named.digest() != digest);
}

TEST_CASE("Swapping the labels or icons of two functions changes the trust digest", "[function_buttons][r3][trust]")
{
    // 신뢰한 뒤 손 편집이나 pull로 두 버튼의 이름을 맞바꾸면 사용자가 고른 것과 다른 명령이 그 이름으로 돈다. 다시 묻는다 (D29).
    const auto digest_of = [](const std::u8string_view first_label, const std::u8string_view first_icon, const std::u8string_view second_label, const std::u8string_view second_icon)
    {
        std::u8string source { u8R"({"schema_version":1,"function_buttons":{"buttons":[)" };
        source += u8R"({"id":"build","label":")" + std::u8string { first_label } + u8R"(","icon":")" + std::u8string { first_icon };
        source += u8R"(","placement":"card","program":"C:\\Tools\\build.exe"},)";
        source += u8R"({"id":"wipe","label":")" + std::u8string { second_label } + u8R"(","icon":")" + std::u8string { second_icon };
        source += u8R"(","placement":"card","program":"C:\\Tools\\wipe.exe","args":["--all"]}]},"projects":[{"id":"alpha","path":"alpha"}]})";
        const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, document_path) };
        REQUIRE(result.document.has_value());
        REQUIRE(result.document->function_buttons.has_value());
        REQUIRE(result.document->function_buttons->buttons.size() == 2u);
        REQUIRE(std::holds_alternative<gitman::function_button>(result.document->function_buttons->buttons[1]));
        return gitman::function_trust_digest(*result.document->function_buttons, result.document->projects);
    };

    const std::u8string digest { digest_of(u8"빌드", u8"tools", u8"지우기", u8"trash") };
    REQUIRE(digest_of(u8"지우기", u8"tools", u8"빌드", u8"trash") != digest);
    REQUIRE(digest_of(u8"빌드", u8"trash", u8"지우기", u8"tools") != digest);
    REQUIRE(digest_of(u8"지우기", u8"trash", u8"빌드", u8"tools") != digest);
}

TEST_CASE("The trust digest input separates fields by length", "[function_buttons][t7]")
{
    // 경계 글자를 값에 넣어도 다른 project 묶음과 같은 입력이 되지 않는다.
    gitman::function_buttons_definition definition {};
    gitman::project_definition first {};
    first.id.value = u8"a b";
    first.path.original = u8"c";
    gitman::project_definition second {};
    second.id.value = u8"a";
    second.path.original = u8"b c";
    const gitman::project_definition one[] { first };
    const gitman::project_definition other[] { second };
    REQUIRE(gitman::function_trust_digest_input(definition, one) != gitman::function_trust_digest_input(definition, other));
    REQUIRE(gitman::function_trust_digest(definition, one) != gitman::function_trust_digest(definition, other));
}

TEST_CASE("Trusted definitions are keyed by the document path and replaced per document", "[function_buttons][t7]")
{
    const std::u8string first(64, u8'a');
    const std::u8string second(64, u8'b');
    gitman::app_settings settings {};
    REQUIRE_FALSE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\team.version-list", first));

    REQUIRE(gitman::trust_function_definition(settings, u8"D:\\ws\\team.version-list", first));
    // 같은 문서는 구분자·대소문자가 달라도 같은 열쇠다.
    REQUIRE(gitman::is_function_definition_trusted(settings, u8"d:/WS/team.version-list", first));
    REQUIRE_FALSE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\team.version-list", second));
    REQUIRE_FALSE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\other.version-list", first));
    REQUIRE_FALSE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\team.version-list", u8""));

    // 같은 해시로 다시 기록하면 바뀐 것이 없다.
    REQUIRE_FALSE(gitman::trust_function_definition(settings, u8"D:\\ws\\team.version-list", first));
    // 새 해시는 옛 해시를 바꾼다. 문서 하나에 항목 하나다.
    REQUIRE(gitman::trust_function_definition(settings, u8"D:\\WS\\team.version-list", second));
    REQUIRE(settings.trusted_function_definitions.size() == 1u);
    REQUIRE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\team.version-list", second));
    REQUIRE_FALSE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\team.version-list", first));

    REQUIRE(gitman::trust_function_definition(settings, u8"D:\\ws\\other.version-list", first));
    REQUIRE(settings.trusted_function_definitions.size() == 2u);

    // 손으로 고친 파일에 같은 문서 항목이 둘이면 다시 신뢰할 때 하나로 줄인다.
    settings.trusted_function_definitions.push_back({ u8"d:/ws/team.version-list", first });
    REQUIRE(gitman::trust_function_definition(settings, u8"D:\\ws\\team.version-list", second));
    REQUIRE(settings.trusted_function_definitions.size() == 2u);
    REQUIRE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\team.version-list", second));
    REQUIRE_FALSE(gitman::is_function_definition_trusted(settings, u8"D:\\ws\\team.version-list", first));
}

TEST_CASE("Trusted definitions round trip through the app settings file and keep unknown keys", "[function_buttons][t7]")
{
    const std::u8string digest(64, u8'c');
    const std::u8string source {
        u8"{\r\n    \"schema_version\": 1,\r\n    \"future_option\": { \"keep\": true },\r\n    \"recent_documents\": [],\r\n"
        u8"    \"trusted_function_definitions\": [\r\n"
        u8"        { \"path\": \"D:\\\\ws\\\\team.version-list\", \"digest\": \""
        + digest
        + u8"\" },\r\n"
        // 읽을 수 없는 항목은 건너뛴다: 해시 모양이 아님, 경로 없음, object 아님.
        u8"        { \"path\": \"D:\\\\ws\\\\bad.version-list\", \"digest\": \"XYZ\" },\r\n"
        u8"        { \"digest\": \""
        + digest
        + u8"\" },\r\n"
        u8"        5\r\n"
        u8"    ]\r\n}\r\n"
    };

    const gitman::app_settings_load_result parsed { gitman::parse_app_settings_json(source, settings_path) };
    REQUIRE(parsed.diagnostics.empty());
    REQUIRE(parsed.settings.trusted_function_definitions.size() == 1u);
    REQUIRE(parsed.settings.trusted_function_definitions.front().document_path == u8"D:\\ws\\team.version-list");
    REQUIRE(parsed.settings.trusted_function_definitions.front().digest == digest);

    gitman::app_settings changed { parsed.settings };
    REQUIRE(gitman::trust_function_definition(changed, u8"D:\\ws\\new.version-list", std::u8string(64, u8'd')));
    const std::u8string serialized { gitman::serialize_app_settings_json(changed, parsed.shadow_source_json) };
    // 알 수 없는 키는 그대로 남는다.
    REQUIRE(serialized.find(u8"future_option") != std::u8string::npos);
    REQUIRE(serialized.find(u8"\"keep\": true") != std::u8string::npos);

    const gitman::app_settings_load_result reparsed { gitman::parse_app_settings_json(serialized, settings_path) };
    REQUIRE(reparsed.diagnostics.empty());
    REQUIRE(reparsed.settings.trusted_function_definitions == changed.trusted_function_definitions);
    REQUIRE(reparsed.settings.recent_documents == changed.recent_documents);
}

TEST_CASE("An empty trust list leaves the app settings file shape alone", "[function_buttons][t7]")
{
    // 신뢰한 적이 없으면 키를 만들지 않는다. 기능 버튼을 쓰지 않는 사용자의 파일이 그대로다.
    const gitman::app_settings settings {};
    const std::u8string serialized { gitman::serialize_app_settings_json(settings, u8"") };
    REQUIRE(serialized.find(u8"trusted_function_definitions") == std::u8string::npos);

    // 형식이 틀린 값은 경고 하나를 내고 무시한다. 신뢰를 기록하기 전까지는 파일 값도 그대로다.
    const std::u8string broken { u8"{ \"schema_version\": 1, \"trusted_function_definitions\": \"x\" }" };
    const gitman::app_settings_load_result parsed { gitman::parse_app_settings_json(broken, settings_path) };
    REQUIRE(parsed.diagnostics.size() == 1u);
    REQUIRE(parsed.settings.trusted_function_definitions.empty());
    REQUIRE(gitman::serialize_app_settings_json(parsed.settings, parsed.shadow_source_json).find(u8"\"trusted_function_definitions\": \"x\"") != std::u8string::npos);
}
