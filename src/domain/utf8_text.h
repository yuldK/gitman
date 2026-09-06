#pragma once

// UTF-8 검사·복구 도구는 luil의 것을 그대로 쓴다 (ADR-007). gitman 코드는 `gitman::`
// 이름으로 계속 부르므로 여기서 전달만 한다. 그리기 앞의 관문(`drawable_text`)은
// luil이 갖고, 밖에서 오는 글을 다듬는 쪽(프로세스 출력·경로)은 이 이름들을 쓴다.
#include "luil/text/utf8_text.h"

namespace gitman {
    using luil::text::utf8_decode;
    using luil::text::utf8_decoded;
    using luil::text::utf8_encode;
    using luil::text::utf8_is_valid;
    using luil::text::utf8_replace_invalid;
    using luil::text::utf8_replacement_character;
} // namespace gitman
