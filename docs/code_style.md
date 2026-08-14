# C++ 코드 컨벤션

이 문서는 Gitman C++ 코드의 기본 작성 규칙이다. 자동으로 표현할 수 있는 규칙은
`.clang-format`과 `scripts/check_source_style.ps1`로 검사한다. 문서에만 표현되는
선호 규칙은 코드 리뷰 기준으로 사용한다.

## 1. 들여쓰기와 중괄호

- 기본 중괄호 형식은 Allman 스타일이다. 여는 중괄호는 제어문, 함수, 타입의
  다음 줄에 둔다. 단, namespace는 `namespace name {`처럼 선언과 같은 줄에
  여는 중괄호를 둔다.
- 들여쓰기는 공백 4칸이며 tab을 사용하지 않는다.
- namespace 내부는 namespace 선언보다 한 단계 깊게 들여쓴다. 중첩된 익명
  namespace도 같은 규칙을 적용한다.
- namespace 여는 중괄호와 첫 선언 사이, 마지막 선언과 namespace 닫는 중괄호
  사이에는 구조를 위한 빈 줄을 두지 않는다. 선언이나 정의 사이를 구분하는
  의미 있는 빈 줄은 허용한다.
- 한 문장으로 끝나는 제어문의 본문은 가급적 중괄호를 생략한다. 이 경우에도
  본문은 제어문과 같은 줄에 쓰지 않고 다음 줄에 둔다.
- 조건식이 여러 줄이면 `&&`와 `||`를 다음 줄의 처음에 둔다.

```cpp
if (ready)
    start();

if (ready
    && connected
    && has_permission)
    start();

if (ready)
{
    start();
    record_start();
}
```

짧다는 이유로 `if (ready) start();`처럼 제어문과 본문을 같은 줄에 두지 않는다.
여러 문장을 묶거나 소유권·수명·조건의 범위가 중요한 경우에는 항상 중괄호를
사용한다.

제어문 앞의 여백은 가독성을 기준으로 둔다. 여러 줄 선언·호출·초기화가 끝난
뒤 제어문이 시작되면 빈 줄을 하나 둔다. 같은 검증 흐름의 연속 guard 제어문은
붙여 둘 수 있으며, 제어문에 붙은 주석은 빈 줄로 분리하지 않는다.

## 2. 표현식과 초기화

- 가능하면 생성, 반환, 대입을 포함한 초기화에 중괄호 초기화를 사용한다.
- 부정 연산자 `!`는 의미를 뒤집는 이름의 predicate, 명시적인 비교 또는
  early-return 등으로 더 읽기 쉬워지는지 먼저 검토한다. 단순 금지가 아니라
  가독성을 위한 지양 규칙이다.
- 긴 표현식이나 범위로 줄바꿈할 때는 마지막 닫는 중괄호 `}` 또는 문장 끝의
  세미콜론 `;`을 다음 줄에 둔다. 닫는 기호는 표현식이 시작한 깊이와 같은
  깊이에 맞춰 표현식의 끝을 시각적으로 표시한다.

```cpp
const auto result = make_result(
    first_value,
    second_value,
    third_value
)
;

const record value {
    .first = first_value,
    .second = second_value
}
;
```

생성자 초기화 목록은 각 항목을 새 줄의 쉼표로 시작한다.

```cpp
record(...)
    : first{ first_value }
    , second{ second_value }
{
    ...
}
```

## 3. namespace

이름 있는 namespace 안의 코드는 한 단계 들여쓴다. 파일 전용 구현은 이름 있는
namespace의 의미적 범위를 유지하면서 익명 namespace에 둔다.

```cpp
namespace gitman {
    namespace {
        void helper()
        {
        }
    } // namespace

    void public_function()
    {
    }
} // namespace gitman
```

namespace 깊이가 과도하게 깊어지면 프로젝트의 namespace 매크로를 사용한다.
매크로를 도입할 때는 대응하는 시작·끝이 한눈에 보이도록 같은 파일 또는 공용
헤더에 두고, 실제 namespace 경계를 주석으로 표시한다.

```cpp
BEGIN_NAMESPACE(gitman::platform::windows)

// implementation

END_NAMESPACE()
```

## 4. 파일 형식과 식별자

- C++, CMake, PowerShell, JSON, XML 및 Windows resource 코드의 기본 들여쓰기는
  공백 4칸이다. tab은 사용하지 않는다. `.editorconfig`가 이 기본값을 편집기에
  전달한다.
- 소스는 UTF-8 무 BOM, CRLF, 줄 끝 공백 없음으로 저장한다.
- 식별자는 `snake_case`를 사용한다. 외부 API, Windows API, third-party API의
  고유 명칭은 원문을 유지할 수 있다.
- template 선언과 함수 signature가 길어져 줄바꿈되는 경우 서로 다른 줄에 둔다.
- 포맷 검사와 source style 검사를 통과해야 한다.
