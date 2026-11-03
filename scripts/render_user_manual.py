"""Render docs/user-manual.md into the single-file docs/user-manual.html.

The page keeps the existing <head> (title and style) of docs/user-manual.html, builds the
table of contents from the ## and ### headings, embeds every image as a data: URI inside a
<figure>, and gives headings GitHub-style ids so the manual's own #links resolve in both
files. Requires markdown-it-py. See docs/handoff.md section 0.2 for when to run it.

    python scripts/render_user_manual.py          # writes docs/user-manual.html
    python scripts/render_user_manual.py --check  # only verifies, exits 1 on a mismatch
"""

from __future__ import annotations

import argparse
import base64
import html
import re
import sys
import unicodedata
from pathlib import Path
from urllib.parse import unquote

from markdown_it import MarkdownIt


ROOT = Path(__file__).resolve().parent.parent
MARKDOWN_PATH = ROOT / "docs" / "user-manual.md"
HTML_PATH = ROOT / "docs" / "user-manual.html"
IMAGE_TYPES = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".gif": "image/gif", ".svg": "image/svg+xml"}


def github_slug(text: str) -> str:
    # GitHub의 제목 앵커 규칙: 소문자로, 글자·숫자·결합 기호·`-`·`_`만 남기고 공백은 `-` 하나씩.
    kept = []
    for character in text.lower():
        if character == " ":
            kept.append("-")
        elif character in "-_" or character.isalnum() or unicodedata.category(character).startswith("M"):
            kept.append(character)
    return "".join(kept)


def inline_text(token) -> str:
    parts = []
    for child in token.children or []:
        if child.type in ("text", "code_inline"):
            parts.append(child.content)
    return "".join(parts)


def image_data_uri(source: str) -> str:
    path = (MARKDOWN_PATH.parent / source).resolve()
    media_type = IMAGE_TYPES.get(path.suffix.lower())
    if media_type is None:
        raise SystemExit(f"unsupported image type: {source}")
    return f"data:{media_type};base64,{base64.b64encode(path.read_bytes()).decode('ascii')}"


def figure_for(match: re.Match[str]) -> str:
    source = html.unescape(match.group(1))
    alt = match.group(2)
    return (
        f'<figure><img src="{image_data_uri(source)}" alt="{alt}" loading="lazy">'
        f"<figcaption>{alt}</figcaption></figure>"
    )


def render(markdown_text: str, head: str) -> tuple[str, dict[str, int]]:
    parser = MarkdownIt("commonmark", {"html": True}).enable("table")
    tokens = parser.parse(markdown_text)

    used: dict[str, int] = {}
    toc: list[tuple[int, str, str]] = []
    for index, token in enumerate(tokens):
        if token.type != "heading_open":
            continue
        title = inline_text(tokens[index + 1])
        slug = github_slug(title)
        if slug in used:
            used[slug] += 1
            slug = f"{slug}-{used[slug]}"
        else:
            used[slug] = 0
        token.attrSet("id", slug)
        level = int(token.tag[1])
        if level in (2, 3):
            toc.append((level, slug, title))

    body = parser.renderer.render(tokens, parser.options, {})
    # 문단에 그림 하나만 있으면 캡션이 붙은 figure로 그린다 (그림 파일은 data: URI로 싣는다).
    body = re.sub(r'<p><img src="([^"]+)" alt="([^"]*)" ?/?></p>', figure_for, body)
    body = body.replace("<table>", '<div class="tablewrap"><table>').replace("</table>", "</table></div>")
    # markdown-it는 링크를 퍼센트 인코딩한다. 문서 안 앵커는 md에 적은 글 그대로 둔다.
    body = re.sub(r'href="#([^"]*)"', lambda match: f'href="#{unquote(match.group(1))}"', body)

    toc_html = ["<ul>"]
    open_child = False
    for position, (level, slug, title) in enumerate(toc):
        link = f'<a href="#{slug}">{html.escape(title, quote=False)}</a>'
        if level == 2:
            if open_child:
                toc_html.append("</ul></li>")
                open_child = False
            elif position > 0:
                toc_html.append("</li>")
            toc_html.append(f"<li>{link}")
        else:
            if open_child is False:
                toc_html.append("<ul>")
                open_child = True
            toc_html.append(f"<li>{link}</li>")
    toc_html.append("</ul></li></ul>" if open_child else "</li></ul>")

    version = re.search(r"^버전 기준: (\d{4}-\d{2}-\d{2})", markdown_text, re.MULTILINE)
    brand_date = version.group(1) if version else ""
    page = (
        head
        + "<body>\n"
        + '<div class="layout">\n'
        + '<nav class="toc">\n'
        + f'  <div class="brand">Gitman<small>사용 설명서 · {brand_date}</small></div>\n'
        + "  "
        + "".join(toc_html)
        + "\n</nav>\n"
        + "<main>\n"
        + body
        + "</main>\n"
        + "</div>\n"
        + '<a class="toplink" href="#">맨 위로</a>\n'
        + "</body>\n"
        + "</html>\n"
    )
    counts = {
        "h2": len([entry for entry in toc if entry[0] == 2]),
        "h3": len([entry for entry in toc if entry[0] == 3]),
    }
    return page, counts


def outside_code(markdown_text: str) -> str:
    # 코드 블록 안의 `|---|`·`## ` 같은 줄은 표나 제목이 아니다.
    kept = []
    inside = False
    for line in markdown_text.split("\n"):
        if line.startswith("```"):
            inside = not inside
            kept.append(line)
        elif inside is False:
            kept.append(line)
    return "\n".join(kept)


def verify(markdown_text: str, page: str) -> list[str]:
    problems = []
    markdown_text = outside_code(markdown_text)
    main = page[page.index("<main>"):page.index("</main>")]
    expected = {
        "h2": len(re.findall(r"^## ", markdown_text, re.MULTILINE)),
        "h3": len(re.findall(r"^### ", markdown_text, re.MULTILINE)),
        "table": len(re.findall(r"^\|[ :|-]*-[ :|-]*\|\s*$", markdown_text, re.MULTILINE)),
        "pre": len(re.findall(r"^```", markdown_text, re.MULTILINE)) // 2,
        "figure": len(re.findall(r"^!\[[^\]]*\]\([^)]+\)\s*$", markdown_text, re.MULTILINE)),
    }
    actual = {
        "h2": main.count("<h2 "),
        "h3": main.count("<h3 "),
        "table": main.count("<table>"),
        "pre": main.count("<pre>"),
        "figure": main.count("<figure>"),
    }
    for name, count in expected.items():
        if actual[name] != count:
            problems.append(f"{name}: markdown {count}, html {actual[name]}")
    ids = set(re.findall(r'id="([^"]+)"', page))
    for target in sorted(set(re.findall(r'href="#([^"]+)"', page))):
        if html.unescape(target) not in ids:
            problems.append(f"link without anchor: #{target}")
    markdown_links = set(re.findall(r"\]\(#([^)]+)\)", markdown_text))
    for target in sorted(markdown_links):
        if target not in ids:
            problems.append(f"markdown link without anchor: #{target}")
    if "<img" in main.replace("<figure><img", ""):
        problems.append("an image outside a figure")
    if re.search(r'src="(?!data:)', main):
        problems.append("an image that is not embedded")
    return problems


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    arguments.add_argument("--check", action="store_true", help="only verify the current html against the markdown")
    options = arguments.parse_args()

    markdown_text = MARKDOWN_PATH.read_text(encoding="utf-8").replace("\r\n", "\n")
    current = HTML_PATH.read_text(encoding="utf-8").replace("\r\n", "\n")
    head_end = current.index("</head>\n") + len("</head>\n")
    page, counts = render(markdown_text, current[:head_end])

    problems = verify(markdown_text, page)
    if options.check:
        if page != current:
            problems.append("docs/user-manual.html is not the rendering of docs/user-manual.md")
    else:
        HTML_PATH.write_bytes(page.replace("\n", "\r\n").encode("utf-8"))
    for problem in problems:
        print(problem, file=sys.stderr)
    print(f"h2 {counts['h2']}, h3 {counts['h3']}, problems {len(problems)}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
