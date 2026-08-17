"""Generate Gitman's Windows icon from the bundled Codicons font."""

from __future__ import annotations

import argparse
import io
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
SUPERSAMPLE = 4
BACKGROUND = (37, 37, 38, 255)
FOREGROUND = (255, 255, 255, 255)


def find_codepoint(mapping_path: Path) -> int:
    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    for codepoint, names in mapping.items():
        if "source-control" in names:
            return int(codepoint)
    raise RuntimeError("The Codicons mapping does not contain source-control.")


def render_icon(size: int, font_path: Path, codepoint: int) -> Image.Image:
    scaled_size = size * SUPERSAMPLE
    image = Image.new("RGBA", (scaled_size, scaled_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    margin = max(1, round(size * 0.05)) * SUPERSAMPLE
    radius = round(size * 0.22) * SUPERSAMPLE
    draw.rounded_rectangle(
        (margin, margin, scaled_size - margin - 1, scaled_size - margin - 1),
        radius=radius,
        fill=BACKGROUND,
    )

    font_size = round(size * 0.70) * SUPERSAMPLE
    font = ImageFont.truetype(str(font_path), font_size)
    draw.text(
        (scaled_size / 2, scaled_size / 2),
        chr(codepoint),
        font=font,
        fill=FOREGROUND,
        anchor="mm",
    )
    return image.resize((size, size), Image.Resampling.LANCZOS)


def write_ico(images: list[Image.Image], output_path: Path) -> None:
    png_data: list[bytes] = []
    for image in images:
        buffer = io.BytesIO()
        image.save(buffer, format="PNG", optimize=True)
        png_data.append(buffer.getvalue())

    header_size = 6
    directory_size = 16 * len(images)
    offset = header_size + directory_size
    directory = bytearray()
    for image, data in zip(images, png_data):
        width = image.width if image.width < 256 else 0
        height = image.height if image.height < 256 else 0
        directory.extend(
            struct.pack(
                "<BBBBHHII",
                width,
                height,
                0,
                0,
                1,
                32,
                len(data),
                offset,
            )
        )
        offset += len(data)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(struct.pack("<HHH", 0, 1, len(images)) + directory + b"".join(png_data))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--mapping", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    codepoint = find_codepoint(arguments.mapping)
    images = [render_icon(size, arguments.font, codepoint) for size in ICON_SIZES]
    write_ico(images, arguments.output)


if __name__ == "__main__":
    main()
