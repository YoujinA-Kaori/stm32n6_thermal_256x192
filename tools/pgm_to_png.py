#!/usr/bin/env python3
"""Convert PGM thermal preview images to PNG.

Usage:
  python tools/pgm_to_png.py --input thermal_out --output thermal_png
  python tools/pgm_to_png.py --input thermal_out/frame_00016241.pgm --output out.png
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import zlib


def read_pgm(input_path: Path) -> tuple[int, int, bytes]:
    """Read a binary PGM (P5) file."""
    raw = input_path.read_bytes()
    index = 0

    def skip_ws_and_comments() -> None:
        nonlocal index
        while index < len(raw):
            if raw[index] in b" \t\r\n":
                index += 1
                continue
            if raw[index] == ord("#"):
                while index < len(raw) and raw[index] not in b"\r\n":
                    index += 1
                continue
            break

    def read_token() -> bytes:
        nonlocal index
        skip_ws_and_comments()
        start = index
        while index < len(raw) and raw[index] not in b" \t\r\n#":
            index += 1
        return raw[start:index]

    magic = read_token()
    if magic != b"P5":
        raise SystemExit(f"{input_path}: unsupported PGM format (expected P5)")

    width = int(read_token())
    height = int(read_token())
    max_value = int(read_token())
    if max_value != 255:
        raise SystemExit(f"{input_path}: unsupported max value {max_value} (expected 255)")

    if index < len(raw) and raw[index] in b" \t\r\n":
        while index < len(raw) and raw[index] in b" \t\r\n":
            index += 1

    pixel_count = width * height
    pixels = raw[index:index + pixel_count]
    if len(pixels) != pixel_count:
        raise SystemExit(f"{input_path}: truncated pixel data")

    return width, height, pixels


def write_png_gray(output_path: Path, width: int, height: int, gray_bytes: bytes) -> None:
    """Write an 8-bit grayscale PNG using only the standard library."""
    def chunk(chunk_type: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + chunk_type
            + data
            + struct.pack(">I", zlib.crc32(chunk_type + data) & 0xFFFFFFFF)
        )

    raw = bytearray()
    row_stride = width
    for row_index in range(height):
        row_start = row_index * row_stride
        raw.append(0)  # filter type 0
        raw.extend(gray_bytes[row_start:row_start + row_stride])

    png = bytearray()
    png.extend(b"\x89PNG\r\n\x1a\n")
    png.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)))
    png.extend(chunk(b"IDAT", zlib.compress(bytes(raw), level=9)))
    png.extend(chunk(b"IEND", b""))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(bytes(png))


def convert_one(input_path: Path, output_path: Path) -> None:
    """Convert one PGM file to PNG."""
    width, height, pixels = read_pgm(input_path)
    write_png_gray(output_path, width, height, pixels)


def build_arg_parser() -> argparse.ArgumentParser:
    """Build command-line arguments."""
    parser = argparse.ArgumentParser(description="Convert thermal PGM images to PNG")
    parser.add_argument("--input", required=True, type=Path, help="PGM file or directory")
    parser.add_argument("--output", required=True, type=Path, help="PNG file or output directory")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    input_path = args.input
    output_path = args.output

    if input_path.is_dir():
        output_path.mkdir(parents=True, exist_ok=True)
        pgm_files = sorted(input_path.glob("*.pgm"))
        for pgm_file in pgm_files:
            convert_one(pgm_file, output_path / (pgm_file.stem + ".png"))
        print(f"converted {len(pgm_files)} file(s)")
        return 0

    if output_path.suffix.lower() != ".png":
        raise SystemExit("for a single input file, --output must be a .png file")

    convert_one(input_path, output_path)
    print(f"converted {input_path} -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
