#!/usr/bin/env python3
"""Parse STM32N6 thermal UART packets.

Packet format:
  - sync_word      uint16  0x55AA (wire bytes: AA 55)
  - packet_type    uint16  0x0001
  - frame_counter  uint32
  - frame_width    uint16
  - frame_height   uint16
  - payload_bytes  uint16
  - center_temp14  uint16
  - min_temp14     uint16
  - max_temp14     uint16
  - payload        frame_width * frame_height uint16 values (little-endian)

Usage examples:
  python tools/uart_temp14_parser.py --port COM6
  python tools/uart_temp14_parser.py --hex-file capture.txt --out out_frames
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import struct
import sys
from array import array
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional

try:
    import serial  # type: ignore
except ImportError as exc:  # pragma: no cover
    serial = None
    _serial_import_error = exc
else:
    _serial_import_error = None


SYNC_WORD = 0x55AA
PACKET_TYPE_TEMP14 = 0x0001
HEADER_FMT = "<HHIHHHHHH"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
SYNC_BYTES = struct.pack("<H", SYNC_WORD)


@dataclass
class ThermalPacket:
    sync_word: int
    packet_type: int
    frame_counter: int
    frame_width: int
    frame_height: int
    payload_bytes: int
    center_temp14: int
    min_temp14: int
    max_temp14: int
    payload: bytes


class PacketParser:
    """Incremental packet parser for a byte stream."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> List[ThermalPacket]:
        """Feed bytes into the parser and return any complete packets."""
        packets: List[ThermalPacket] = []
        if not data:
            return packets

        self._buffer.extend(data)

        while True:
            sync_index = self._buffer.find(SYNC_BYTES)
            if sync_index < 0:
                if len(self._buffer) > 1:
                    del self._buffer[:-1]
                break

            if sync_index > 0:
                del self._buffer[:sync_index]

            if len(self._buffer) < HEADER_SIZE:
                break

            header = struct.unpack_from(HEADER_FMT, self._buffer, 0)
            sync_word, packet_type, frame_counter, frame_width, frame_height, payload_bytes, center_temp14, min_temp14, max_temp14 = header

            if sync_word != SYNC_WORD:
                del self._buffer[0]
                continue

            expected_payload_bytes = int(frame_width) * int(frame_height) * 2
            total_bytes = HEADER_SIZE + payload_bytes

            if payload_bytes != expected_payload_bytes:
                del self._buffer[0]
                continue

            if len(self._buffer) < total_bytes:
                break

            payload = bytes(self._buffer[HEADER_SIZE:total_bytes])
            del self._buffer[:total_bytes]

            packets.append(
                ThermalPacket(
                    sync_word=sync_word,
                    packet_type=packet_type,
                    frame_counter=frame_counter,
                    frame_width=frame_width,
                    frame_height=frame_height,
                    payload_bytes=payload_bytes,
                    center_temp14=center_temp14,
                    min_temp14=min_temp14,
                    max_temp14=max_temp14,
                    payload=payload,
                )
            )

        return packets


def parse_hex_text(text: str) -> bytes:
    """Convert a space-separated hex dump into raw bytes."""
    tokens = re.findall(r"(?i)\b(?:0x)?([0-9a-f]{2})\b", text)
    return bytes(int(token, 16) for token in tokens)


def temp14_payload_to_array(payload: bytes) -> array:
    """Convert little-endian uint16 payload bytes to an array of integers."""
    values = array("H")
    values.frombytes(payload)
    if sys.byteorder != "little":
        values.byteswap()
    return values


def normalize_to_u8(values: array, min_value: int, max_value: int) -> bytes:
    """Normalize uint16 values to 8-bit grayscale."""
    if max_value <= min_value:
        return bytes([0] * len(values))

    scale = 255.0 / float(max_value - min_value)
    out = bytearray(len(values))
    for index, value in enumerate(values):
        level = int((value - min_value) * scale)
        if level < 0:
            level = 0
        elif level > 255:
            level = 255
        out[index] = level
    return bytes(out)


def save_pgm(path: Path, width: int, height: int, gray_bytes: bytes) -> None:
    """Write a binary PGM image."""
    with path.open("wb") as fp:
        fp.write(f"P5\n{width} {height}\n255\n".encode("ascii"))
        fp.write(gray_bytes)


def save_csv(path: Path, width: int, height: int, values: array) -> None:
    """Write the frame as CSV."""
    with path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        for row_index in range(height):
            row_start = row_index * width
            writer.writerow(values[row_start:row_start + width])


def ensure_out_dir(base_dir: Path) -> Path:
    """Create output directory if needed."""
    base_dir.mkdir(parents=True, exist_ok=True)
    return base_dir


def handle_packet(packet: ThermalPacket, out_dir: Path, save_bin: bool, save_pgm_flag: bool, save_csv_flag: bool) -> None:
    """Print packet info and optionally save outputs."""
    values = temp14_payload_to_array(packet.payload)
    computed_min = min(values) if values else 0
    computed_max = max(values) if values else 0
    center_index = (packet.frame_height // 2) * packet.frame_width + (packet.frame_width // 2)
    center_value = values[center_index] if 0 <= center_index < len(values) else 0

    print(
        f"frame={packet.frame_counter} type=0x{packet.packet_type:04X} "
        f"{packet.frame_width}x{packet.frame_height} payload={packet.payload_bytes} "
        f"center={packet.center_temp14} min={packet.min_temp14} max={packet.max_temp14} "
        f"computed_min={computed_min} computed_max={computed_max}"
    )

    frame_tag = f"frame_{packet.frame_counter:08d}"

    if save_bin:
        (out_dir / f"{frame_tag}.bin").write_bytes(packet.payload)

    if save_csv_flag:
        save_csv(out_dir / f"{frame_tag}.csv", packet.frame_width, packet.frame_height, values)

    if save_pgm_flag:
        min_value = packet.min_temp14 if packet.max_temp14 > packet.min_temp14 else computed_min
        max_value = packet.max_temp14 if packet.max_temp14 > packet.min_temp14 else computed_max
        gray = normalize_to_u8(values, min_value, max_value)
        save_pgm(out_dir / f"{frame_tag}.pgm", packet.frame_width, packet.frame_height, gray)

    if center_value != packet.center_temp14:
        print(f"warning: center mismatch (header={packet.center_temp14}, computed={center_value})")


def parse_hex_file(hex_file: Path, out_dir: Path, save_bin: bool, save_pgm_flag: bool, save_csv_flag: bool) -> None:
    """Parse a text hex dump file."""
    parser = PacketParser()
    data = parse_hex_text(hex_file.read_text(encoding="utf-8", errors="ignore"))
    for packet in parser.feed(data):
        handle_packet(packet, out_dir, save_bin, save_pgm_flag, save_csv_flag)


def parse_serial(port: str, baud: int, out_dir: Path, save_bin: bool, save_pgm_flag: bool, save_csv_flag: bool, limit: Optional[int]) -> None:
    """Parse packets from a serial port."""
    if serial is None:  # pragma: no cover
        raise SystemExit(f"pyserial is required: {_serial_import_error}")

    parser = PacketParser()
    packet_count = 0

    with serial.Serial(port=port, baudrate=baud, bytesize=8, parity="N", stopbits=1, timeout=0.5) as ser:
        print(f"listening on {port} @ {baud} ...")
        while True:
            chunk = ser.read(4096)
            for packet in parser.feed(chunk):
                handle_packet(packet, out_dir, save_bin, save_pgm_flag, save_csv_flag)
                packet_count += 1
                if limit is not None and packet_count >= limit:
                    return


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the command-line argument parser."""
    parser = argparse.ArgumentParser(description="STM32N6 thermal UART packet parser")
    parser.add_argument("--port", default="COM6", help="Serial port, default COM6")
    parser.add_argument("--baud", type=int, default=2000000, help="Serial baud rate")
    parser.add_argument("--hex-file", type=Path, help="Parse a text hex dump instead of live serial data")
    parser.add_argument("--out", type=Path, default=Path("thermal_out"), help="Output directory")
    parser.add_argument("--save-bin", action="store_true", help="Save raw payload as .bin")
    parser.add_argument("--save-pgm", action="store_true", help="Save grayscale preview as .pgm")
    parser.add_argument("--save-csv", action="store_true", help="Save frame data as .csv")
    parser.add_argument("--limit", type=int, default=None, help="Stop after N packets when reading serial")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    out_dir = ensure_out_dir(args.out)

    if args.hex_file:
        parse_hex_file(args.hex_file, out_dir, args.save_bin, args.save_pgm, args.save_csv)
        return 0

    parse_serial(args.port, args.baud, out_dir, args.save_bin, args.save_pgm, args.save_csv, args.limit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
