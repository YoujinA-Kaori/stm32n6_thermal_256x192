from __future__ import annotations

import argparse
import asyncio
import base64
import json
import logging
import os
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

ROOT_DIR = Path(__file__).resolve().parents[1]
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

try:
    import serial  # type: ignore
except ImportError as exc:  # pragma: no cover
    serial = None
    SERIAL_IMPORT_ERROR = exc
else:
    SERIAL_IMPORT_ERROR = None

try:
    from serial.tools import list_ports  # type: ignore
except ImportError:  # pragma: no cover
    list_ports = None

from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from starlette.concurrency import run_in_threadpool

from tools.uart_temp14_parser import PACKET_TYPE_TEMP14, PacketParser, ThermalPacket, temp14_payload_to_array

APP_DIR = Path(__file__).resolve().parent
STATIC_DIR = APP_DIR / "static"
DOWNLOAD_DIR = APP_DIR / "downloads"
DEFAULT_BAUD = 2000000
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8000
AUTO_SERIAL_PORT = "auto"

DOWNLOAD_DIR.mkdir(parents=True, exist_ok=True)
LOGGER = logging.getLogger("thermal_web")
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")


def temp14_to_celsius(temp14_value: int) -> float:
    """Convert a temp14 value to degrees Celsius."""
    return (float(temp14_value) / 16.0) - 273.15


def list_serial_ports() -> list[dict[str, str]]:
    """List serial ports available on the host."""
    if list_ports is None:  # pragma: no cover
        return []

    ports: list[dict[str, str]] = []
    for port_info in list_ports.comports():
        ports.append(
            {
                "device": port_info.device,
                "description": port_info.description or port_info.device,
                "hwid": port_info.hwid or "",
            }
        )
    return ports


def build_rgb565_bmp_bytes(width: int, height: int, pixel_bytes: bytes) -> bytes:
    """Build one top-down RGB565 BMP file from raw pixel bytes."""
    if width <= 0 or height <= 0:
        raise ValueError("invalid BMP dimensions")

    image_size = width * height * 2
    if len(pixel_bytes) != image_size:
        raise ValueError(f"unexpected RGB565 payload size: got {len(pixel_bytes)}, want {image_size}")

    header_size = 14 + 40 + 12
    file_size = header_size + image_size
    header = bytearray(header_size)
    header[0:2] = b"BM"
    header[2:6] = file_size.to_bytes(4, "little")
    header[10:14] = header_size.to_bytes(4, "little")
    header[14:18] = (40).to_bytes(4, "little")
    header[18:22] = width.to_bytes(4, "little", signed=False)
    header[22:26] = (-height).to_bytes(4, "little", signed=True)
    header[26:28] = (1).to_bytes(2, "little")
    header[28:30] = (16).to_bytes(2, "little")
    header[30:34] = (3).to_bytes(4, "little")
    header[34:38] = image_size.to_bytes(4, "little")
    header[38:42] = (2835).to_bytes(4, "little")
    header[42:46] = (2835).to_bytes(4, "little")
    header[54:58] = (0xF800).to_bytes(4, "little")
    header[58:62] = (0x07E0).to_bytes(4, "little")
    header[62:66] = (0x001F).to_bytes(4, "little")
    return bytes(header) + pixel_bytes


def pick_unique_download_path(file_name: str) -> Path:
    """Create a non-conflicting destination path inside the downloads folder."""
    candidate = DOWNLOAD_DIR / file_name
    if not candidate.exists():
        return candidate

    stem = candidate.stem
    suffix = candidate.suffix
    for index in range(1, 1000):
        numbered = DOWNLOAD_DIR / f"{stem}_{index}{suffix}"
        if not numbered.exists():
            return numbered

    raise RuntimeError("too many duplicate download names")


@dataclass
class FrameSnapshot:
    """Latest thermal frame snapshot."""

    frame_counter: int
    frame_width: int
    frame_height: int
    center_temp14: int
    min_temp14: int
    max_temp14: int
    min_x: int
    min_y: int
    max_x: int
    max_y: int
    payload_b64: str
    timestamp: float

    def to_wire_dict(self) -> dict[str, object]:
        """Serialize the frame to a browser-friendly payload."""
        return {
            "frame_counter": self.frame_counter,
            "frame_width": self.frame_width,
            "frame_height": self.frame_height,
            "center_temp14": self.center_temp14,
            "center_temp_c": round(temp14_to_celsius(self.center_temp14), 2),
            "min_temp14": self.min_temp14,
            "min_temp_c": round(temp14_to_celsius(self.min_temp14), 2),
            "max_temp14": self.max_temp14,
            "max_temp_c": round(temp14_to_celsius(self.max_temp14), 2),
            "min_x": self.min_x,
            "min_y": self.min_y,
            "max_x": self.max_x,
            "max_y": self.max_y,
            "payload_b64": self.payload_b64,
            "timestamp": self.timestamp,
        }


class FrameHub:
    """Thread-safe latest-frame cache and serial reader controller."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._transaction_lock = threading.Lock()
        self._snapshot: Optional[FrameSnapshot] = None
        self._status_text = "待连接"
        self._serial_port = ""
        self._baud_rate = DEFAULT_BAUD
        self._reader_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()

    def configure(self, serial_port: str, baud_rate: int) -> None:
        """Configure the serial source."""
        with self._lock:
            self._serial_port = serial_port.strip()
            self._baud_rate = baud_rate

    def current_port(self) -> str:
        """Return the configured serial port name."""
        with self._lock:
            return self._serial_port

    def current_baud_rate(self) -> int:
        """Return the configured baud rate."""
        with self._lock:
            return self._baud_rate

    def set_status(self, status_text: str) -> None:
        """Update the human-readable status text."""
        with self._lock:
            self._status_text = status_text

    def status_dict(self) -> dict[str, object]:
        """Return the current status snapshot."""
        with self._lock:
            latest = self._snapshot
            return {
                "status": self._status_text,
                "serial_port": self._serial_port,
                "baud_rate": self._baud_rate,
                "has_frame": latest is not None,
                "latest_frame_counter": latest.frame_counter if latest is not None else None,
                "frame_width": latest.frame_width if latest is not None else None,
                "frame_height": latest.frame_height if latest is not None else None,
            }

    def latest(self) -> Optional[FrameSnapshot]:
        """Return the most recent frame snapshot."""
        with self._lock:
            return self._snapshot

    def available_ports(self) -> list[dict[str, str]]:
        """Return the currently available serial ports."""
        return list_serial_ports()

    def connect(self, serial_port: str, baud_rate: Optional[int] = None) -> None:
        """Switch the active serial port and restart the reader."""
        normalized_port = serial_port.strip() if serial_port is not None else ""
        if not normalized_port:
            normalized_port = AUTO_SERIAL_PORT

        if baud_rate is not None:
            with self._lock:
                self._baud_rate = baud_rate

        with self._lock:
            self._serial_port = normalized_port

        self.stop_reader()
        self.start_reader()

    def update_from_packet(self, packet: ThermalPacket) -> None:
        """Convert one thermal packet into a browser snapshot."""
        if packet.packet_type != PACKET_TYPE_TEMP14:
            return

        values = temp14_payload_to_array(packet.payload)
        if len(values) == 0:
            return

        frame_width = int(packet.frame_width)
        frame_height = int(packet.frame_height)
        min_temp14 = int(values[0])
        max_temp14 = int(values[0])
        min_x = 0
        min_y = 0
        max_x = 0
        max_y = 0

        for pixel_index, pixel_temp14 in enumerate(values):
            pixel_x = pixel_index % frame_width
            pixel_y = pixel_index // frame_width

            if pixel_temp14 < min_temp14:
                min_temp14 = int(pixel_temp14)
                min_x = pixel_x
                min_y = pixel_y

            if pixel_temp14 > max_temp14:
                max_temp14 = int(pixel_temp14)
                max_x = pixel_x
                max_y = pixel_y

        center_index = (frame_height // 2) * frame_width + (frame_width // 2)
        center_temp14 = int(values[center_index]) if center_index < len(values) else 0

        snapshot = FrameSnapshot(
            frame_counter=int(packet.frame_counter),
            frame_width=frame_width,
            frame_height=frame_height,
            center_temp14=center_temp14,
            min_temp14=min_temp14,
            max_temp14=max_temp14,
            min_x=min_x,
            min_y=min_y,
            max_x=max_x,
            max_y=max_y,
            payload_b64=base64.b64encode(packet.payload).decode("ascii"),
            timestamp=time.time(),
        )

        with self._lock:
            self._snapshot = snapshot
            self._status_text = f"实时预览 {self._serial_port} @ {self._baud_rate}"

    def _resolve_serial_settings(self) -> tuple[str, int]:
        """Resolve the serial port and baud rate used by either reader or file session."""
        with self._lock:
            serial_port = self._serial_port
            baud_rate = self._baud_rate

        if not serial_port or serial_port == AUTO_SERIAL_PORT:
            ports = list_serial_ports()
            if len(ports) == 0:
                raise RuntimeError("未发现串口")
            serial_port = ports[0]["device"]
            with self._lock:
                self._serial_port = serial_port

        if not serial_port:
            raise RuntimeError("未配置串口")

        return serial_port, baud_rate

    def start_reader(self) -> None:
        """Start the background serial reader."""
        if self._reader_thread is not None and self._reader_thread.is_alive():
            return

        if serial is None:  # pragma: no cover
            self.set_status(f"缺少 pyserial: {SERIAL_IMPORT_ERROR}")
            return

        try:
            serial_port, baud_rate = self._resolve_serial_settings()
        except RuntimeError as exc:
            self.set_status(str(exc))
            return

        self._stop_event.clear()
        self._reader_thread = threading.Thread(
            target=self._reader_loop,
            args=(serial_port, baud_rate),
            daemon=True,
            name="thermal-serial-reader",
        )
        self._reader_thread.start()

    def stop_reader(self) -> None:
        """Stop the background serial reader."""
        self._stop_event.set()
        thread = self._reader_thread
        if thread is not None and thread.is_alive():
            thread.join(timeout=1.0)
        self._reader_thread = None

    def _reader_loop(self, serial_port: str, baud_rate: int) -> None:
        """Read packets from the serial port and update the cache."""
        parser = PacketParser()
        self.set_status(f"正在监听 {serial_port} @ {baud_rate}")

        try:
            with serial.Serial(  # type: ignore[union-attr]
                port=serial_port,
                baudrate=baud_rate,
                bytesize=8,
                parity="N",
                stopbits=1,
                timeout=0.5,
            ) as ser:
                while not self._stop_event.is_set():
                    chunk = ser.read(4096)
                    if not chunk:
                        continue

                    for packet in parser.feed(chunk):
                        self.update_from_packet(packet)
        except Exception as exc:  # pragma: no cover
            self.set_status(f"串口错误: {exc}")

    def _send_ascii_command(self, ser: "serial.Serial", command_text: str) -> None:
        """Send one newline-terminated ASCII command."""
        packet = (command_text.strip() + "\n").encode("ascii")
        ser.write(packet)
        ser.flush()

    def _flush_input(self, ser: "serial.Serial", settle_seconds: float = 0.12) -> None:
        """Drop any buffered bytes before starting a file transaction."""
        ser.reset_input_buffer()
        time.sleep(settle_seconds)
        ser.reset_input_buffer()

    def _read_ascii_line(self, ser: "serial.Serial", timeout_seconds: float = 5.0) -> str:
        """Read one newline-terminated ASCII line from the MCU."""
        deadline = time.time() + timeout_seconds
        line_buffer = bytearray()
        while time.time() < deadline:
            next_byte = ser.read(1)
            if not next_byte:
                continue
            line_buffer.extend(next_byte)
            if next_byte == b"\n":
                return line_buffer.decode("ascii", errors="ignore").strip()
        raise TimeoutError("串口命令超时")

    def _read_protocol_line(
        self,
        ser: "serial.Serial",
        accepted_prefixes: tuple[str, ...],
        timeout_seconds: float = 5.0,
    ) -> str:
        """Read one MCU protocol line while ignoring binary stream residue."""
        deadline = time.time() + timeout_seconds
        while time.time() < deadline:
            line = self._read_ascii_line(ser, timeout_seconds=max(0.2, min(1.0, deadline - time.time())))
            if not line:
                continue
            if any(line.startswith(prefix) for prefix in accepted_prefixes):
                return line
        raise TimeoutError("串口协议重同步超时")

    def _ensure_ok_response(self, ser: "serial.Serial", command_text: str) -> str:
        """Send one command and require an OK line."""
        self._send_ascii_command(ser, command_text)
        response = self._read_protocol_line(ser, ("OK ", "ERR "), timeout_seconds=15.0)
        if not response.startswith("OK "):
            raise RuntimeError(f"{command_text} 失败: {response}")
        return response

    def _run_file_session(self, session_fn: Callable[["serial.Serial"], dict[str, object]]) -> dict[str, object]:
        """Run one exclusive file transaction by temporarily pausing the real-time reader."""
        if serial is None:  # pragma: no cover
            raise RuntimeError(f"缺少 pyserial: {SERIAL_IMPORT_ERROR}")

        with self._transaction_lock:
            self.stop_reader()
            try:
                serial_port, baud_rate = self._resolve_serial_settings()
                self.set_status(f"文件模式 {serial_port} @ {baud_rate}")
                with serial.Serial(  # type: ignore[union-attr]
                    port=serial_port,
                    baudrate=baud_rate,
                    bytesize=8,
                    parity="N",
                    stopbits=1,
                    timeout=1.0,
                    write_timeout=2.0,
                ) as ser:
                    self._flush_input(ser)
                    return session_fn(ser)
            finally:
                self.start_reader()

    def list_sd_gallery(self) -> dict[str, object]:
        """Fetch the snapshot list from the MCU SD gallery."""
        last_error: Optional[Exception] = None
        for attempt in range(3):
            try:
                def session(ser: "serial.Serial") -> dict[str, object]:
                    files: list[str] = []
                    self._ensure_ok_response(ser, "FILE_ENTER")
                    try:
                        self._send_ascii_command(ser, "FILE_LIST")
                        line = self._read_protocol_line(ser, ("LIST ", "ERR "), timeout_seconds=15.0)
                        if not line.startswith("LIST "):
                            raise RuntimeError(f"无效列表响应: {line}")

                        while True:
                            line = self._read_protocol_line(ser, ("NAME ", "LIST_END", "ERR "), timeout_seconds=10.0)
                            if line == "LIST_END":
                                break
                            if line.startswith("NAME "):
                                files.append(line[5:])
                            elif line.startswith("ERR "):
                                raise RuntimeError(line)
                        return {"files": files, "count": len(files)}
                    finally:
                        try:
                            self._ensure_ok_response(ser, "FILE_EXIT")
                        except Exception:
                            pass

                return self._run_file_session(session)
            except Exception as exc:
                last_error = exc
                LOGGER.warning("list_sd_gallery attempt %s failed: %s", attempt + 1, exc)
                time.sleep(0.25)
        raise RuntimeError(str(last_error) if last_error is not None else "图库同步失败")

    def download_latest_snapshot(self) -> dict[str, object]:
        """Download the latest SD snapshot and store it on the host PC."""
        return self._download_snapshot_by_command("FILE_GET_LATEST")

    def download_snapshot_named(self, file_name: str) -> dict[str, object]:
        """Download one selected SD snapshot by file name."""
        normalized_name = file_name.strip()
        if not normalized_name:
            raise RuntimeError("未指定要下载的文件")
        return self._download_snapshot_by_command(f"FILE_GET {normalized_name}")

    def _download_snapshot_by_command(self, command_text: str) -> dict[str, object]:
        """Run the common MCU file-download flow for one FILE_GET command."""
        last_error: Optional[Exception] = None
        for attempt in range(3):
            try:
                def session(ser: "serial.Serial") -> dict[str, object]:
                    self._ensure_ok_response(ser, "FILE_ENTER")
                    try:
                        self._send_ascii_command(ser, command_text)
                        line = self._read_protocol_line(ser, ("FILE_BEGIN ", "ERR "), timeout_seconds=15.0)
                        if line.startswith("ERR "):
                            raise RuntimeError(line)
                        if not line.startswith("FILE_BEGIN "):
                            raise RuntimeError(f"无效文件头: {line}")

                        parts = line.split(" ", 4)
                        if len(parts) != 5:
                            raise RuntimeError(f"文件头字段不完整: {line}")

                        file_name = parts[1]
                        width = int(parts[2])
                        height = int(parts[3])
                        payload_size = int(parts[4])
                        payload_bytes = bytearray()
                        expected_chunks = (payload_size + 383) // 384

                        for _ in range(expected_chunks):
                            line = self._read_protocol_line(ser, ("DATA ", "ERR "), timeout_seconds=10.0)
                            if line.startswith("ERR "):
                                raise RuntimeError(line)
                            if not line.startswith("DATA "):
                                raise RuntimeError(f"无效数据包: {line}")

                            payload_parts = line.split(" ", 2)
                            if len(payload_parts) != 3:
                                raise RuntimeError(f"数据包字段不完整: {line}")
                            payload_bytes.extend(base64.b64decode(payload_parts[2]))

                        line = self._read_protocol_line(ser, ("FILE_END", "ERR "), timeout_seconds=10.0)
                        if line.startswith("ERR "):
                            raise RuntimeError(line)
                        if line != "FILE_END":
                            raise RuntimeError(f"文件结束标记异常: {line}")

                        if len(payload_bytes) != payload_size:
                            raise RuntimeError(f"文件长度不匹配: got {len(payload_bytes)}, want {payload_size}")

                        bmp_bytes = build_rgb565_bmp_bytes(width, height, bytes(payload_bytes))
                        save_path = pick_unique_download_path(file_name)
                        save_path.write_bytes(bmp_bytes)

                        return {
                            "file_name": file_name,
                            "width": width,
                            "height": height,
                            "payload_bytes": payload_size,
                            "saved_name": save_path.name,
                            "saved_path": str(save_path),
                            "download_url": f"/downloads/{save_path.name}",
                        }
                    finally:
                        try:
                            self._ensure_ok_response(ser, "FILE_EXIT")
                        except Exception:
                            pass

                return self._run_file_session(session)
            except Exception as exc:
                last_error = exc
                LOGGER.warning("download_snapshot attempt %s failed: %s", attempt + 1, exc)
                time.sleep(0.35)
        raise RuntimeError(str(last_error) if last_error is not None else "最新截图下载失败")


HUB = FrameHub()
app = FastAPI(title="热成像网页查看器")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")
app.mount("/downloads", StaticFiles(directory=DOWNLOAD_DIR), name="downloads")


@app.on_event("startup")
async def on_startup() -> None:
    """Start the serial reader after the app boots."""
    serial_port = os.environ.get("THERMAL_SERIAL_PORT", AUTO_SERIAL_PORT).strip()
    baud_rate_text = os.environ.get("THERMAL_SERIAL_BAUD", str(DEFAULT_BAUD))
    try:
        baud_rate = int(baud_rate_text)
    except ValueError:
        baud_rate = DEFAULT_BAUD

    HUB.configure(serial_port, baud_rate)
    HUB.start_reader()


@app.on_event("shutdown")
async def on_shutdown() -> None:
    """Stop the serial reader before the app exits."""
    HUB.stop_reader()


@app.get("/")
async def index() -> FileResponse:
    """Serve the main viewer page."""
    return FileResponse(STATIC_DIR / "index.html")


@app.get("/api/status")
async def api_status() -> JSONResponse:
    """Return the current viewer status."""
    return JSONResponse(HUB.status_dict())


@app.get("/api/latest")
async def api_latest() -> JSONResponse:
    """Return the most recent frame snapshot."""
    snapshot = HUB.latest()
    if snapshot is None:
        return JSONResponse({"error": "暂无帧数据"}, status_code=404)

    return JSONResponse(snapshot.to_wire_dict())


@app.get("/api/ports")
async def api_ports() -> JSONResponse:
    """Return host serial port candidates."""
    return JSONResponse(
        {
            "current_port": HUB.current_port(),
            "ports": HUB.available_ports(),
        }
    )


@app.post("/api/connect")
async def api_connect(request: Request) -> JSONResponse:
    """Switch the active serial port."""
    body = await request.json()
    serial_port = str(body.get("serial_port", AUTO_SERIAL_PORT))
    baud_rate_value = body.get("baud_rate")
    baud_rate = None
    if baud_rate_value is not None:
        try:
            baud_rate = int(baud_rate_value)
        except (TypeError, ValueError):
            baud_rate = None

    HUB.connect(serial_port, baud_rate)
    return JSONResponse(
        {
            "status": HUB.status_dict(),
            "ports": HUB.available_ports(),
        }
    )


@app.post("/api/gallery/list")
async def api_gallery_list() -> JSONResponse:
    """Fetch the SD snapshot list via the serial file protocol."""
    try:
        result = await run_in_threadpool(HUB.list_sd_gallery)
    except Exception as exc:
        return JSONResponse({"error": str(exc)}, status_code=500)
    return JSONResponse(result)


@app.post("/api/gallery/download-latest")
async def api_gallery_download_latest() -> JSONResponse:
    """Download the latest SD snapshot to the host machine."""
    try:
        result = await run_in_threadpool(HUB.download_latest_snapshot)
    except Exception as exc:
        return JSONResponse({"error": str(exc)}, status_code=500)
    return JSONResponse(result)


@app.post("/api/gallery/download")
async def api_gallery_download(request: Request) -> JSONResponse:
    """Download one selected SD snapshot to the host machine."""
    body = await request.json()
    file_name = str(body.get("file_name", ""))
    try:
        result = await run_in_threadpool(HUB.download_snapshot_named, file_name)
    except Exception as exc:
        return JSONResponse({"error": str(exc)}, status_code=500)
    return JSONResponse(result)


@app.websocket("/ws")
async def ws_endpoint(websocket: WebSocket) -> None:
    """Push latest frames to the browser."""
    await websocket.accept()
    last_frame_counter = -1
    last_status_signature: Optional[tuple[object, object, object]] = None
    try:
        while True:
            status_data = HUB.status_dict()
            status_signature = (
                status_data.get("status"),
                status_data.get("serial_port"),
                status_data.get("baud_rate"),
            )
            if status_signature != last_status_signature:
                await websocket.send_text(json.dumps({"type": "status", "data": status_data}, ensure_ascii=False))
                last_status_signature = status_signature

            snapshot = HUB.latest()
            if snapshot is not None and snapshot.frame_counter != last_frame_counter:
                payload = {"type": "frame", "data": snapshot.to_wire_dict()}
                await websocket.send_text(json.dumps(payload, ensure_ascii=False))
                last_frame_counter = snapshot.frame_counter
            await asyncio.sleep(0.1)
    except WebSocketDisconnect:
        return


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the command-line parser."""
    parser = argparse.ArgumentParser(description="热成像串口网页查看器")
    parser.add_argument("--serial", default=os.environ.get("THERMAL_SERIAL_PORT", ""), help="串口名，例如 COM5")
    parser.add_argument("--baud", type=int, default=int(os.environ.get("THERMAL_SERIAL_BAUD", str(DEFAULT_BAUD))), help="串口波特率")
    parser.add_argument("--host", default=os.environ.get("THERMAL_WEB_HOST", DEFAULT_HOST), help="HTTP 绑定主机")
    parser.add_argument("--port", type=int, default=int(os.environ.get("THERMAL_WEB_PORT", str(DEFAULT_PORT))), help="HTTP 绑定端口")
    return parser


def main() -> int:
    """Start the web viewer."""
    args = build_arg_parser().parse_args()
    os.environ["THERMAL_SERIAL_PORT"] = args.serial
    os.environ["THERMAL_SERIAL_BAUD"] = str(args.baud)

    import uvicorn

    uvicorn.run(app, host=args.host, port=args.port, reload=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
