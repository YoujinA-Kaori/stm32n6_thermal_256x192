#!/usr/bin/env python3

"""Generate a tiny LVGL font from a source font and a small character set.

The script is intentionally narrow: it extracts Chinese glyphs used by the GUI,
renders them with Pillow, and emits a plain LVGL fmt_txt font with sparse
cmap mapping plus Montserrat fallback for ASCII.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Iterable, List, Sequence

from PIL import ImageFont


CHINESE_RE = re.compile(r"[\u4e00-\u9fff℃℉]")


def extract_chars_from_files(paths: Sequence[Path]) -> List[str]:
    """Extract the ordered unique glyph set from the given text files."""
    chars = set()
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="ignore")
        chars.update(CHINESE_RE.findall(text))

    return sorted(chars, key=ord)


def pack_4bpp(pixels: bytes) -> bytes:
    """Pack 8-bit grayscale pixels into LVGL 4bpp raster order."""
    packed = bytearray()
    for index in range(0, len(pixels), 2):
        first = (pixels[index] * 15 + 127) // 255
        second = 0
        if index + 1 < len(pixels):
            second = (pixels[index + 1] * 15 + 127) // 255
        packed.append((first << 4) | second)
    return bytes(packed)


def render_glyphs(font_path: Path, size_px: int, chars: Sequence[str]):
    """Render glyphs and return the LVGL bitmap stream and descriptors."""
    font = ImageFont.truetype(str(font_path), size_px)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent

    glyph_bitmap = bytearray()
    glyph_dsc = []
    unicode_list = []

    for ch in chars:
        bbox = font.getbbox(ch, anchor="ls")
        if bbox is None:
            bbox = (0, 0, 0, 0)

        mask = font.getmask(ch, mode="L", anchor="ls")
        width, height = mask.size
        pixels = bytes(mask)

        if width * height != len(pixels):
            raise RuntimeError(f"Unexpected mask size for {ch!r}: {width}x{height} != {len(pixels)}")

        bitmap_index = len(glyph_bitmap)
        glyph_bitmap.extend(pack_4bpp(pixels))

        adv_w = int(round(font.getlength(ch) * 16.0))
        ofs_x = int(bbox[0])
        ofs_y = int(ascent + bbox[1])

        glyph_dsc.append(
            {
                "bitmap_index": bitmap_index,
                "adv_w": adv_w,
                "box_w": width,
                "box_h": height,
                "ofs_x": ofs_x,
                "ofs_y": ofs_y,
            }
        )
        unicode_list.append(ord(ch))

    return glyph_bitmap, glyph_dsc, unicode_list, line_height, base_line


def emit_font_c(
    output_path: Path,
    font_symbol: str,
    fallback_font: str,
    glyph_bitmap: bytes,
    glyph_dsc: Sequence[dict],
    unicode_list: Sequence[int],
    line_height: int,
    base_line: int,
    range_start: int,
    range_length: int,
) -> None:
    """Write a standalone LVGL font C file."""
    font_macro = f"LV_FONT_{font_symbol[8:].upper()}"
    unicode_rel = [value - range_start for value in unicode_list]

    output = [
        "/*",
        " * Auto-generated tiny Chinese subset font for the thermal GUI.",
        " * Source: SimSun.woff",
        f" * Size: {line_height} px",
        " * Bpp: 4",
        " */",
        "",
        "#ifdef LV_LVGL_H_INCLUDE_SIMPLE",
        "#include \"lvgl.h\"",
        "#else",
        "#include \"lvgl.h\"",
        "#endif",
        "",
        f"LV_FONT_DECLARE({fallback_font});",
        "",
        f"#ifndef {font_macro}",
        f"#define {font_macro} 1",
        "#endif",
        "",
        f"#if {font_macro}",
        "",
        "/*-----------------",
        " *    BITMAPS",
        " *----------------*/",
        "",
        "/*Store the image of the glyphs*/",
        "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {",
    ]

    for index in range(0, len(glyph_bitmap), 12):
        chunk = glyph_bitmap[index:index + 12]
        output.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")

    output.extend(
        [
            "};",
            "",
            "/*-----------------",
            " *  GLYPH DESCRIPTORS",
            " *----------------*/",
            "",
            "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {",
            "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,",
        ]
    )

    for dsc in glyph_dsc:
        output.append(
            "    {"
            f".bitmap_index = {dsc['bitmap_index']}, .adv_w = {dsc['adv_w']}, "
            f".box_w = {dsc['box_w']}, .box_h = {dsc['box_h']}, "
            f".ofs_x = {dsc['ofs_x']}, .ofs_y = {dsc['ofs_y']}}},"
        )

    output.extend(
        [
            "};",
            "",
            "/*-----------------",
            " *    CMAPS",
            " *----------------*/",
            "",
            "static const uint16_t unicode_list_0[] = {",
        ]
    )

    for index in range(0, len(unicode_rel), 12):
        chunk = unicode_rel[index:index + 12]
        output.append("    " + ", ".join(f"0x{value:04x}" for value in chunk) + ",")

    output.extend(
        [
            "};",
            "",
            "static const lv_font_fmt_txt_cmap_t cmaps[] =",
            "{",
            "    {",
            f"        .range_start = {range_start}, .range_length = {range_length}, .glyph_id_start = 1,",
            f"        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = {len(unicode_list)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
            "    }",
            "};",
            "",
            "/*-----------------",
            " *   CUSTOM DATA",
            " *----------------*/",
            "",
            "#if LVGL_VERSION_MAJOR == 8",
            "static lv_font_fmt_txt_glyph_cache_t cache;",
            "#endif",
            "",
            "#if LVGL_VERSION_MAJOR >= 8",
            "static const lv_font_fmt_txt_dsc_t font_dsc = {",
            "#else",
            "static lv_font_fmt_txt_dsc_t font_dsc = {",
            "#endif",
            "    .glyph_bitmap = glyph_bitmap,",
            "    .glyph_dsc = glyph_dsc,",
            "    .cmaps = cmaps,",
            "    .kern_dsc = NULL,",
            "    .kern_scale = 0,",
            "    .cmap_num = 1,",
            "    .bpp = 4,",
            "    .kern_classes = 0,",
            "    .bitmap_format = 0,",
            "#if LVGL_VERSION_MAJOR == 8",
            "    .cache = &cache",
            "#endif",
            "};",
            "",
            "/*-----------------",
        " *  PUBLIC FONT",
        " *----------------*/",
        "",
        "#if LVGL_VERSION_MAJOR >= 8",
        f"const lv_font_t {font_symbol} = {{",
        "#else",
        f"lv_font_t {font_symbol} = {{",
        "#endif",
            "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,",
            "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
            f"    .line_height = {line_height},",
            f"    .base_line = {base_line},",
            "#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)",
            "    .subpx = LV_FONT_SUBPX_NONE,",
            "#endif",
            "#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8",
            "    .underline_position = -1,",
            "    .underline_thickness = 1,",
            "#endif",
            "    .dsc = &font_dsc,",
            "#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9",
            f"    .fallback = &{fallback_font},",
            "#endif",
            "    .user_data = NULL,",
            "};",
            "",
            "#endif /*#if LV_FONT_...*/",
            "",
        ]
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(output), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a tiny LVGL Chinese subset font.")
    parser.add_argument("--font", required=True, type=Path, help="Source font file (TTF/WOFF)")
    parser.add_argument("--size", type=int, default=18, help="Font size in pixels")
    parser.add_argument("--output", required=True, type=Path, help="Output LVGL C file")
    parser.add_argument("--fallback-font", default="lv_font_montserratMedium_19",
                        help="LVGL fallback font symbol")
    parser.add_argument("--source", type=Path, action="append", default=[],
                        help="Text source to extract characters from")
    parser.add_argument("--extra", default="", help="Extra characters to force into the subset")
    args = parser.parse_args()

    chars = extract_chars_from_files(args.source) if args.source else []
    chars.extend(list(args.extra))
    chars = sorted(set(chars), key=ord)

    if not chars:
        raise SystemExit("No characters found for the subset font.")

    glyph_bitmap, glyph_dsc, unicode_list, line_height, base_line = render_glyphs(args.font, args.size, chars)
    range_start = min(unicode_list)
    range_length = max(unicode_list) - range_start + 1

    emit_font_c(
        output_path=args.output,
        font_symbol=f"lv_font_thermal_cn_{args.size}",
        fallback_font=args.fallback_font,
        glyph_bitmap=glyph_bitmap,
        glyph_dsc=glyph_dsc,
        unicode_list=unicode_list,
        line_height=line_height,
        base_line=base_line,
        range_start=range_start,
        range_length=range_length,
    )

    print(f"Generated {args.output} with {len(chars)} glyphs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
