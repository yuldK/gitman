#include "domain/function_trust.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gitman {
    namespace {
        constexpr std::array<std::uint32_t, 64> round_constants {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u
            , 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u
            , 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau
            , 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u
            , 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u
            , 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u
            , 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u
            , 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        constexpr std::uint32_t rotate_right(const std::uint32_t value, const int count) noexcept
        {
            return (value >> count) | (value << (32 - count));
        }

        // 64 byte 블록 하나를 상태에 더한다.
        void compress(std::array<std::uint32_t, 8>& state, const unsigned char* const block) noexcept
        {
            std::array<std::uint32_t, 64> words {};
            for (std::size_t index = 0; index < 16; ++index)
            {
                const unsigned char* const word { block + index * 4 };
                words[index] = (static_cast<std::uint32_t>(word[0]) << 24)
                    | (static_cast<std::uint32_t>(word[1]) << 16)
                    | (static_cast<std::uint32_t>(word[2]) << 8)
                    | static_cast<std::uint32_t>(word[3]);
            }
            for (std::size_t index = 16; index < 64; ++index)
            {
                const std::uint32_t low { rotate_right(words[index - 15], 7) ^ rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3) };
                const std::uint32_t high { rotate_right(words[index - 2], 17) ^ rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10) };
                words[index] = words[index - 16] + low + words[index - 7] + high;
            }

            std::array<std::uint32_t, 8> work { state };
            for (std::size_t index = 0; index < 64; ++index)
            {
                const std::uint32_t sum1 { rotate_right(work[4], 6) ^ rotate_right(work[4], 11) ^ rotate_right(work[4], 25) };
                const std::uint32_t choose { (work[4] & work[5]) ^ (~work[4] & work[6]) };
                const std::uint32_t first { work[7] + sum1 + choose + round_constants[index] + words[index] };
                const std::uint32_t sum0 { rotate_right(work[0], 2) ^ rotate_right(work[0], 13) ^ rotate_right(work[0], 22) };
                const std::uint32_t majority { (work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]) };
                const std::uint32_t second { sum0 + majority };
                work[7] = work[6];
                work[6] = work[5];
                work[5] = work[4];
                work[4] = work[3] + first;
                work[3] = work[2];
                work[2] = work[1];
                work[1] = work[0];
                work[0] = first + second;
            }
            for (std::size_t index = 0; index < 8; ++index)
                state[index] += work[index];
        }

        void append_decimal(std::u8string& text, std::size_t value)
        {
            std::u8string digits {};
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + value % 10));
                value /= 10;
            } while (value > 0);
            text += digits;
        }

        // `<byte 길이>:<글>`이다. 글 안에 어떤 글자가 있어도 다음 구간과 섞이지 않는다.
        void append_field(std::u8string& text, const std::u8string_view value)
        {
            append_decimal(text, value.size());
            text.push_back(u8':');
            text.append(value);
        }
    }

    std::u8string sha256_hex(const std::u8string_view bytes)
    {
        std::array<std::uint32_t, 8> state {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
        };

        // 마지막 블록에 `0x80`, 0 채움, 64 bit 길이(bit 단위, big endian)를 붙인다.
        std::vector<unsigned char> message(bytes.begin(), bytes.end());
        const std::uint64_t bit_length { static_cast<std::uint64_t>(bytes.size()) * 8u };
        message.push_back(0x80u);
        while (message.size() % 64 != 56)
            message.push_back(0u);
        for (int shift = 56; shift >= 0; shift -= 8)
            message.push_back(static_cast<unsigned char>((bit_length >> shift) & 0xffu));

        for (std::size_t offset = 0; offset < message.size(); offset += 64)
            compress(state, message.data() + offset);

        constexpr std::u8string_view hex_digits { u8"0123456789abcdef" };
        std::u8string text {};
        text.reserve(64);
        for (const std::uint32_t word : state)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
                text.push_back(hex_digits[(word >> shift) & 0xfu]);
        }
        return text;
    }

    std::u8string function_trust_digest_input(const function_buttons_definition& definition, const std::span<const project_definition> projects)
    {
        // 카드 순서는 사용자가 끌어서 바꾸는 표시 상태다. 순서만 바뀐 문서가 신뢰를 다시 묻지 않게
        // id로 정렬한다. id가 같은 project는 파서가 들이지 않지만 경로까지 비교해 결과를 정한다.
        std::vector<const project_definition*> ordered {};
        ordered.reserve(projects.size());
        for (const project_definition& project : projects)
            ordered.push_back(&project);
        std::ranges::sort(ordered
            , [](const project_definition* const left, const project_definition* const right)
            {
                if (left->id.value != right->id.value)
                    return left->id.value < right->id.value;
                return left->path.original < right->path.original;
            }
        );

        std::u8string text { u8"gitman.function-trust.v1\n" };
        text += u8"function_buttons ";
        append_field(text, definition.source_digest_input);
        text.push_back(u8'\n');
        for (const project_definition* const project : ordered)
        {
            text += u8"project ";
            append_field(text, project->id.value);
            text.push_back(u8' ');
            append_field(text, project->path.original);
            text.push_back(u8' ');
            // `${projectName}`과 `GITMAN_PROJECT_NAME`으로 프로그램에 들어가는 실행 입력이다. gitman에는
            // 표시 이름을 바꾸는 길이 없어 넣어도 다시 묻는 경우는 손으로 고친 때뿐이다.
            append_field(text, function_project_name(*project));
            text.push_back(u8'\n');
        }
        return text;
    }

    std::u8string function_trust_digest(const function_buttons_definition& definition, const std::span<const project_definition> projects)
    {
        return sha256_hex(function_trust_digest_input(definition, projects));
    }
}
