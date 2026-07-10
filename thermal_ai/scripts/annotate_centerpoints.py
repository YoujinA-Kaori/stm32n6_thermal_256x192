#!/usr/bin/env python3
"""交互式热像框标注工具，支持在工具内完成样本归类。"""

from __future__ import annotations

import argparse
import shutil
import tkinter as tk
from pathlib import Path
from tkinter import messagebox
from typing import Any

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    AnnotatedObject,
    annotation_path_for_bin,
    get_class_names,
    get_detection_class_names,
    load_dataset_config,
    load_json,
    load_sidecar_annotation,
    load_temp14_frame,
    normalize_sidecar_annotation,
    resolve_workspace_path,
    save_json,
    temp14_preview_u8,
)


class AnnotationTool:
    """多目标热像框标注工具。"""

    def __init__(
        self,
        root: tk.Tk,
        bin_paths: list[Path],
        config: dict[str, Any],
        scale: int,
        raw_root: Path,
        input_root: Path | None,
        session_name: str | None,
        session_prefix: str | None,
        auto_samples_per_session: int,
        startup_message: str | None = None,
    ) -> None:
        try:
            from PIL import Image, ImageDraw, ImageTk
        except ImportError as exc:  # pragma: no cover
            raise SystemExit(f"标注工具依赖 Pillow，请先安装：{exc}") from exc

        self.Image = Image
        self.ImageDraw = ImageDraw
        self.ImageTk = ImageTk
        self.root = root
        self.bin_paths = bin_paths
        self.config = config
        self.scale = max(int(scale), 1)
        self.class_names = get_class_names(config)
        self.detection_class_names = get_detection_class_names(config)
        self.raw_root = raw_root
        self.input_root = input_root
        self.fixed_session_name = session_name.strip() if session_name is not None and session_name.strip() else None
        self.auto_session_prefix = session_prefix.strip() if session_prefix is not None and session_prefix.strip() else None
        self.auto_samples_per_session = max(int(auto_samples_per_session), 0)
        self.auto_session_index = 1
        self.auto_session_saved_count = 0
        self.current_session_name: str | None = None
        self.startup_message = startup_message
        self.index = 0
        self.current_primary_class_name: str | None = None
        self.current_objects: list[AnnotatedObject] = []
        self.selected_object_class = self.detection_class_names[0] if self.detection_class_names else "circuit_board_normal"
        self.photo_image = None
        self.preview_image = None
        self.frame_width = int(config["input"]["width"])
        self.frame_height = int(config["input"]["height"])
        self.drag_start_xy: tuple[int, int] | None = None
        self.drag_current_xy: tuple[int, int] | None = None
        self.class_colors = {
            "circuit_board_normal": "#52c41a",
            "circuit_board_abnormal_hotspot": "#36cfc9",
        }
        self.class_display_names = {
            "circuit_board_normal": "电路板正常",
            "circuit_board_abnormal_hotspot": "电路板异常热点",
        }

        self.root.title("热像框标注工具")
        self.root.geometry(f"{self.frame_width * self.scale + 180}x{self.frame_height * self.scale + 380}")

        self.info_var = tk.StringVar(value="")
        self.status_var = tk.StringVar(value="")
        self.session_var = tk.StringVar(value="")
        self.selected_class_var = tk.StringVar(value="")
        self.primary_class_var = tk.StringVar(value="")
        self.object_count_var = tk.StringVar(value="")
        self.object_list_var = tk.StringVar(value="")

        self._initialize_session_strategy()

        self._build_ui()
        self._bind_keys()
        self._load_current_sample()

    @staticmethod
    def _sanitize_session_token(raw_value: str | None, fallback: str) -> str:
        """Normalize one session/group token into a filesystem-safe name."""
        if raw_value is None:
            candidate = fallback
        else:
            candidate = raw_value.strip()
            if not candidate:
                candidate = fallback

        safe_chars = []
        for char in candidate:
            if char.isalnum() or char in ("_", "-"):
                safe_chars.append(char)
            else:
                safe_chars.append("_")

        normalized = "".join(safe_chars).strip("_-")
        return normalized if normalized else fallback

    def _is_external_input_root(self) -> bool:
        """Return whether the current input root is outside the managed raw dataset tree."""
        if self.input_root is None:
            return False

        try:
            self.input_root.relative_to(self.raw_root)
            return False
        except ValueError:
            return True

    def _should_copy_source_artifacts(self, source_bin_path: Path) -> bool:
        """Return whether source artifacts should be copied instead of moved."""
        candidate_roots: list[Path] = []
        if self.input_root is not None:
            candidate_roots.append(self.input_root if self.input_root.is_dir() else self.input_root.parent)

        workspace_thermal_out = resolve_workspace_path(Path("thermal_out"))
        if workspace_thermal_out.exists():
            candidate_roots.append(workspace_thermal_out)

        source_resolved = source_bin_path.resolve()
        for root_path in candidate_roots:
            try:
                root_resolved = root_path.resolve()
                source_resolved.relative_to(root_resolved)
            except ValueError:
                continue

            if root_resolved.name.lower() == "thermal_out":
                return True

        return False

    def _format_auto_session_name(self, session_index: int) -> str:
        """Build one auto-generated coarse grouping name."""
        prefix = self._sanitize_session_token(self.auto_session_prefix, "session_auto")
        return f"{prefix}_{session_index:03d}"

    def _initialize_session_strategy(self) -> None:
        """Resolve whether this run uses a fixed session, existing folders, or auto-grouping."""
        if self.fixed_session_name is not None:
            self.fixed_session_name = self._sanitize_session_token(self.fixed_session_name, "session_fixed")
            self.current_session_name = self.fixed_session_name
            return

        if self.auto_session_prefix is None and self._is_external_input_root():
            default_prefix = self.input_root.name if self.input_root is not None else "session_auto"
            self.auto_session_prefix = self._sanitize_session_token(default_prefix, "session_auto")

        if self.auto_session_prefix is not None:
            self.auto_session_prefix = self._sanitize_session_token(self.auto_session_prefix, "session_auto")
            self.current_session_name = self._format_auto_session_name(self.auto_session_index)

    def _advance_auto_session(self) -> str:
        """Advance to the next coarse grouping bucket and return its new name."""
        if self.auto_session_prefix is None:
            default_prefix = self.input_root.name if self.input_root is not None else "session_auto"
            self.auto_session_prefix = self._sanitize_session_token(default_prefix, "session_auto")

        self.auto_session_index += 1
        self.auto_session_saved_count = 0
        self.current_session_name = self._format_auto_session_name(self.auto_session_index)
        return self.current_session_name

    def _current_session_display_text(self, bin_path: Path | None = None) -> str:
        """Return one short status string describing the active save bucket."""
        if self.fixed_session_name is not None:
            return f"固定分组: {self.fixed_session_name}"

        if self.auto_session_prefix is not None and self.current_session_name is not None:
            if self.auto_samples_per_session > 0:
                return (
                    f"自动分组: {self.current_session_name} "
                    f"({self.auto_session_saved_count}/{self.auto_samples_per_session})"
                )
            return f"自动分组: {self.current_session_name}"

        if bin_path is not None:
            return f"当前目录分组: {self._derive_target_session_name(bin_path)}"
        return "当前目录分组"

    def advance_session_group(self) -> None:
        """Manually roll over to the next coarse grouping bucket."""
        if self.fixed_session_name is not None:
            self._update_status("当前处于固定分组模式，不能手动切换到新分组")
            return

        if self.auto_session_prefix is None and not self._is_external_input_root():
            self._update_status("当前输入已经在 raw 目录内；如需手动分组，请启动时传入 --session-prefix")
            return

        previous_session_name = self.current_session_name
        next_session_name = self._advance_auto_session()
        self.session_var.set(self._current_session_display_text(self.bin_paths[self.index] if self.bin_paths else None))
        if previous_session_name is None:
            self._update_status(f"已启用自动分组：{next_session_name}")
        else:
            self._update_status(f"已切换到新分组：{next_session_name}")

    def _build_ui(self) -> None:
        """构建主界面。"""
        top_frame = tk.Frame(self.root)
        top_frame.pack(fill=tk.X, padx=8, pady=8)
        tk.Label(top_frame, textvariable=self.info_var, anchor="w", justify=tk.LEFT, font=("Microsoft YaHei UI", 10)).pack(fill=tk.X)
        tk.Label(top_frame, textvariable=self.status_var, anchor="w", justify=tk.LEFT, fg="#0B5", font=("Microsoft YaHei UI", 10)).pack(fill=tk.X)
        tk.Label(top_frame, textvariable=self.session_var, anchor="w", justify=tk.LEFT, fg="#555555", font=("Microsoft YaHei UI", 10)).pack(fill=tk.X)

        preview_frame = tk.Frame(self.root)
        preview_frame.pack(fill=tk.BOTH, expand=False, padx=8)
        self.canvas = tk.Canvas(
            preview_frame,
            width=self.frame_width * self.scale,
            height=self.frame_height * self.scale,
            bg="black",
            highlightthickness=1,
            highlightbackground="#666666",
        )
        self.canvas.pack()
        self.canvas.bind("<ButtonPress-1>", self._on_left_press)
        self.canvas.bind("<B1-Motion>", self._on_left_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_left_release)
        self.canvas.bind("<Button-3>", self._on_right_click)

        control_frame = tk.Frame(self.root)
        control_frame.pack(fill=tk.X, padx=8, pady=8)
        tk.Label(control_frame, text="框类别", font=("Microsoft YaHei UI", 10)).grid(row=0, column=0, sticky="w")
        tk.Label(control_frame, textvariable=self.selected_class_var, font=("Microsoft YaHei UI", 10, "bold")).grid(row=0, column=1, sticky="w", padx=(4, 12))
        tk.Label(control_frame, text="归档类别", font=("Microsoft YaHei UI", 10)).grid(row=0, column=2, sticky="w")
        tk.Label(control_frame, textvariable=self.primary_class_var, font=("Microsoft YaHei UI", 10, "bold")).grid(row=0, column=3, sticky="w", padx=(4, 12))
        tk.Label(control_frame, text="目标数", font=("Microsoft YaHei UI", 10)).grid(row=0, column=4, sticky="w")
        tk.Label(control_frame, textvariable=self.object_count_var, font=("Microsoft YaHei UI", 10, "bold")).grid(row=0, column=5, sticky="w", padx=(4, 12))

        button_frame = tk.Frame(self.root)
        button_frame.pack(fill=tk.X, padx=8)
        buttons = [
            ("保存并下一张", self.save_and_next),
            ("设为主类别", self.set_primary_class_to_selected),
            ("上一张", self.previous_sample),
            ("下一张", self.next_sample),
            ("删除最后框", self.delete_last_object),
            ("清空当前框", self.clear_objects),
            ("退出", self.root.destroy),
        ]
        for index, (label, command) in enumerate(buttons):
            tk.Button(button_frame, text=label, command=command, width=12).grid(row=0, column=index, padx=3, pady=3)
        tk.Button(button_frame, text="新分组", command=self.advance_session_group, width=12).grid(row=1, column=0, padx=3, pady=3, sticky="w")

        object_list_frame = tk.Frame(self.root)
        object_list_frame.pack(fill=tk.X, padx=8, pady=8)
        tk.Label(object_list_frame, text="当前标注框", anchor="w", justify=tk.LEFT, font=("Microsoft YaHei UI", 9, "bold")).pack(fill=tk.X)
        tk.Label(
            object_list_frame,
            textvariable=self.object_list_var,
            anchor="w",
            justify=tk.LEFT,
            font=("Consolas", 9),
            fg="#333333",
        ).pack(fill=tk.X)

        max_key = len(self.detection_class_names)
        help_lines = [
            "操作说明：",
            f"1. 按数字键 1-{max_key} 选择当前要画的框类别。",
            "2. 鼠标左键拖拽画框。",
            "3. 按 P 可以把当前选中的框类别设为这张样本的归档类别。",
            "4. 按 S 或 Enter 保存并切到下一张。",
            "5. 如果输入来自 thermal_out，保存时会自动把样本复制到 raw/<类别>/<session>/，不会删除原始文件。",
        ]
        help_lines.append("6. 按 G 或点击“新分组”可以在你切场景时手动开启下一批。")
        help_lines.append("7. 从 thermal_out 导入时，工具可以自动按小批次归档，减少你手工管理 session 的压力。")
        tk.Label(
            self.root,
            text="\n".join(help_lines),
            justify=tk.LEFT,
            anchor="w",
            font=("Microsoft YaHei UI", 9),
            fg="#444444",
        ).pack(fill=tk.X, padx=8, pady=8)

    def _bind_keys(self) -> None:
        """注册快捷键。"""
        self.root.bind("<Return>", lambda _event: self.save_and_next())
        self.root.bind("s", lambda _event: self.save_and_next())
        self.root.bind("S", lambda _event: self.save_and_next())
        self.root.bind("p", lambda _event: self.set_primary_class_to_selected())
        self.root.bind("P", lambda _event: self.set_primary_class_to_selected())
        self.root.bind("g", lambda _event: self.advance_session_group())
        self.root.bind("G", lambda _event: self.advance_session_group())
        self.root.bind("<Left>", lambda _event: self.previous_sample())
        self.root.bind("<Right>", lambda _event: self.next_sample())
        self.root.bind("<BackSpace>", lambda _event: self.delete_last_object())
        self.root.bind("<Delete>", lambda _event: self.delete_last_object())
        self.root.bind("c", lambda _event: self.clear_objects())
        self.root.bind("C", lambda _event: self.clear_objects())
        self.root.bind("<Escape>", lambda _event: self.root.destroy())
        self.root.bind("q", lambda _event: self.root.destroy())
        self.root.bind("Q", lambda _event: self.root.destroy())

        for index, class_name in enumerate(self.detection_class_names, start=1):
            self.root.bind(str(index), lambda _event, name=class_name: self._set_selected_class(name))

    def _relative_display_path(self, bin_path: Path) -> str:
        """生成紧凑显示路径。"""
        for root_path in (self.raw_root, self.input_root):
            if root_path is None:
                continue
            try:
                return str(bin_path.relative_to(root_path))
            except ValueError:
                continue
        return str(bin_path)

    def _derive_primary_class_name(self, bin_path: Path) -> str | None:
        """根据目录结构推断主类别。"""
        try:
            relative = bin_path.relative_to(self.raw_root)
            if relative.parts:
                first_part = relative.parts[0]
                if first_part in self.class_names:
                    return first_part
        except ValueError:
            pass

        for parent in bin_path.parents:
            if parent.name in self.class_names:
                return parent.name
        return None

    def _derive_target_session_name(self, bin_path: Path) -> str:
        """保存样本时推断目标 session 目录。"""
        if self.fixed_session_name is not None:
            return self.fixed_session_name

        if self.auto_session_prefix is not None and self.current_session_name is not None:
            return self.current_session_name

        try:
            relative = bin_path.relative_to(self.raw_root)
            if len(relative.parts) >= 3:
                return relative.parts[1]
        except ValueError:
            pass

        if self.input_root is not None:
            try:
                relative = bin_path.relative_to(self.input_root)
                if len(relative.parts) > 1:
                    return "__".join(relative.parts[:-1]).replace(" ", "_")
            except ValueError:
                pass
            if self.input_root.is_dir():
                return self.input_root.name

        if bin_path.parent.name:
            return bin_path.parent.name
        return "session_default"

    def _derive_target_class_name(self) -> str | None:
        """解析当前样本的目标类别目录。"""
        if self.current_primary_class_name is not None:
            return self.current_primary_class_name
        if self.current_objects:
            return self.current_objects[0].class_name
        return None

    def _build_target_bin_path(self, source_bin_path: Path, target_class_name: str, session_name: str) -> Path:
        """构建样本保存到 raw 数据集后的目标路径。"""
        return self.raw_root / target_class_name / session_name / source_bin_path.name

    def _move_sample_artifacts(self, source_bin_path: Path, target_bin_path: Path) -> None:
        """Copy or move source .bin and sidecar artifacts into the target directory."""
        if source_bin_path == target_bin_path:
            return

        target_dir = target_bin_path.parent
        target_dir.mkdir(parents=True, exist_ok=True)
        source_artifacts = sorted(path for path in source_bin_path.parent.glob(f"{source_bin_path.stem}.*") if path.is_file())
        copy_source = self._should_copy_source_artifacts(source_bin_path)

        for source_artifact_path in source_artifacts:
            target_artifact_path = target_dir / source_artifact_path.name
            if source_artifact_path == target_artifact_path:
                continue
            if target_artifact_path.exists():
                raise FileExistsError(f"目标文件已存在：{target_artifact_path}")

        for source_artifact_path in source_artifacts:
            target_artifact_path = target_dir / source_artifact_path.name
            if source_artifact_path == target_artifact_path:
                continue
            if copy_source:
                shutil.copy2(str(source_artifact_path), str(target_artifact_path))
            else:
                shutil.move(str(source_artifact_path), str(target_artifact_path))

    def _clamp_canvas_xy(self, canvas_x: int, canvas_y: int) -> tuple[int, int]:
        """把画布坐标限制在有效热像范围内。"""
        frame_x = max(0, min(self.frame_width - 1, int(canvas_x / self.scale)))
        frame_y = max(0, min(self.frame_height - 1, int(canvas_y / self.scale)))
        return frame_x, frame_y

    def _load_current_sample(self) -> None:
        """加载当前样本到界面。"""
        if not self.bin_paths:
            raise SystemExit("没有可供标注的 .bin 文件")

        bin_path = self.bin_paths[self.index]
        frame_u16 = load_temp14_frame(bin_path, self.config)
        preview_u8 = temp14_preview_u8(frame_u16, self.config)
        preview_image = self.Image.fromarray(preview_u8, mode="L").convert("RGB")
        preview_image = preview_image.resize((self.frame_width * self.scale, self.frame_height * self.scale), self.Image.NEAREST)

        annotation_payload = None
        annotation_path = annotation_path_for_bin(bin_path)
        if annotation_path.exists():
            annotation_payload = load_json(annotation_path)

        annotation = normalize_sidecar_annotation(annotation_payload)
        derived_primary = self._derive_primary_class_name(bin_path)
        self.current_primary_class_name = (
            annotation.primary_class_name
            if annotation is not None and annotation.primary_class_name is not None
            else derived_primary
            if derived_primary is not None
            else None
        )
        self.current_objects = list(annotation.objects) if annotation is not None else []
        self.drag_start_xy = None
        self.drag_current_xy = None
        if self.current_objects and self.selected_object_class not in self.detection_class_names:
            self.selected_object_class = self.current_objects[0].class_name

        self.preview_image = preview_image
        self._refresh_canvas()
        self._update_status(self.startup_message or "")
        self.startup_message = None

    def _refresh_canvas(self) -> None:
        """重绘预览图和所有框。"""
        if self.preview_image is None:
            return

        overlay = self.preview_image.copy()
        draw = self.ImageDraw.Draw(overlay)

        for object_index, annotated_object in enumerate(self.current_objects, start=1):
            color = self.class_colors.get(annotated_object.class_name, "#ff4d4f")
            x_min = int(round(annotated_object.x_min * self.scale))
            y_min = int(round(annotated_object.y_min * self.scale))
            x_max = int(round(annotated_object.x_max * self.scale))
            y_max = int(round(annotated_object.y_max * self.scale))
            draw.rectangle((x_min, y_min, x_max, y_max), outline=color, width=max(1, self.scale // 2))
            draw.text((x_min + 2, max(0, y_min - 14)), f"{object_index}", fill=color)

        if self.drag_start_xy is not None and self.drag_current_xy is not None:
            x0, y0 = self.drag_start_xy
            x1, y1 = self.drag_current_xy
            draft_box = (
                min(x0, x1) * self.scale,
                min(y0, y1) * self.scale,
                max(x0, x1) * self.scale,
                max(y0, y1) * self.scale,
            )
            draft_color = self.class_colors.get(self.selected_object_class, "#ff4d4f")
            draw.rectangle(draft_box, outline=draft_color, width=max(1, self.scale // 2))

        self.photo_image = self.ImageTk.PhotoImage(overlay)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.photo_image)

        self.info_var.set(f"[{self.index + 1}/{len(self.bin_paths)}] {self._relative_display_path(self.bin_paths[self.index])}")
        self.session_var.set(self._current_session_display_text(self.bin_paths[self.index]))
        self.selected_class_var.set(self._display_class_name(self.selected_object_class))
        self.primary_class_var.set(
            self._display_class_name(self.current_primary_class_name)
            if self.current_primary_class_name is not None
            else "（未设置）"
        )
        self.object_count_var.set(str(len(self.current_objects)))
        if self.current_objects:
            object_lines = [
                f"{index + 1}. {self._display_class_name(annotated_object.class_name):<12} [{int(annotated_object.x_min):3d},{int(annotated_object.y_min):3d}] -> [{int(annotated_object.x_max):3d},{int(annotated_object.y_max):3d}]"
                for index, annotated_object in enumerate(self.current_objects)
            ]
            self.object_list_var.set("\n".join(object_lines))
        else:
            self.object_list_var.set("（当前还没有标注框）")

    def _update_status(self, message: str) -> None:
        """更新状态栏文案。"""
        annotation_path = annotation_path_for_bin(self.bin_paths[self.index])
        status = f"标注文件：{annotation_path.name}"
        if message:
            status += f" | {message}"
        self.status_var.set(status)

    def _display_class_name(self, class_name: str | None) -> str:
        """把内部类别名转换为中文显示名。"""
        if class_name is None:
            return ""
        return self.class_display_names.get(class_name, class_name)

    def _set_selected_class(self, class_name: str) -> None:
        """切换当前要画的框类别。"""
        self.selected_object_class = class_name
        self.selected_class_var.set(self._display_class_name(class_name))
        self._refresh_canvas()
        self._update_status(f"当前画框类别已切换为：{self._display_class_name(class_name)}")

    def set_primary_class_to_selected(self) -> None:
        """把当前选中的框类别设为样本归档类别。"""
        self.current_primary_class_name = self.selected_object_class
        self._refresh_canvas()
        self._update_status(f"当前样本归档到：{self._display_class_name(self.selected_object_class)}")

    def _on_left_press(self, event: tk.Event) -> None:
        """开始拖拽画框。"""
        self.drag_start_xy = self._clamp_canvas_xy(event.x, event.y)
        self.drag_current_xy = self.drag_start_xy
        self._refresh_canvas()

    def _on_left_drag(self, event: tk.Event) -> None:
        """拖拽时更新临时框。"""
        if self.drag_start_xy is None:
            return
        self.drag_current_xy = self._clamp_canvas_xy(event.x, event.y)
        self._refresh_canvas()

    def _on_left_release(self, event: tk.Event) -> None:
        """松开鼠标后提交一个框。"""
        if self.drag_start_xy is None:
            return

        end_xy = self._clamp_canvas_xy(event.x, event.y)
        start_x, start_y = self.drag_start_xy
        end_x, end_y = end_xy
        x_min = min(start_x, end_x)
        y_min = min(start_y, end_y)
        x_max = max(start_x, end_x) + 1
        y_max = max(start_y, end_y) + 1

        self.drag_start_xy = None
        self.drag_current_xy = None
        if (x_max - x_min) < 2 or (y_max - y_min) < 2:
            self._refresh_canvas()
            self._update_status("框太小，已忽略")
            return

        self.current_objects.append(
            AnnotatedObject(
                class_name=self.selected_object_class,
                x_min=float(x_min),
                y_min=float(y_min),
                x_max=float(x_max),
                y_max=float(y_max),
            )
        )
        if self.current_primary_class_name is None:
            self.current_primary_class_name = self.selected_object_class
        self._refresh_canvas()
        self._update_status(
            f"已添加 {self._display_class_name(self.selected_object_class)} 标注框 "
            f"[{x_min},{y_min}] -> [{x_max},{y_max}]"
        )

    def _on_right_click(self, event: tk.Event) -> None:
        """删除离点击位置最近的标注框。"""
        if not self.current_objects:
            self._update_status("当前没有可删除的标注框")
            return

        x, y = self._clamp_canvas_xy(event.x, event.y)
        containing_indices = [
            index
            for index, annotated_object in enumerate(self.current_objects)
            if annotated_object.x_min <= x <= annotated_object.x_max and annotated_object.y_min <= y <= annotated_object.y_max
        ]
        if containing_indices:
            remove_index = containing_indices[-1]
        else:
            remove_index = min(
                range(len(self.current_objects)),
                key=lambda index: (self.current_objects[index].center_x - x) ** 2 + (self.current_objects[index].center_y - y) ** 2,
            )

        removed = self.current_objects.pop(remove_index)
        self._refresh_canvas()
        self._update_status(
            f"已删除 {self._display_class_name(removed.class_name)} 标注框 "
            f"[{int(removed.x_min)},{int(removed.y_min)}] -> [{int(removed.x_max)},{int(removed.y_max)}]"
        )

    def _build_annotation_payload(self) -> dict[str, Any]:
        """把当前界面状态序列化成 JSON 标注格式。"""
        primary_class_name = self.current_primary_class_name if self.current_primary_class_name is not None else self.selected_object_class
        return {
            "primary_class_name": primary_class_name,
            "objects": [
                {
                    "class_name": annotated_object.class_name,
                    "x_min": int(annotated_object.x_min),
                    "y_min": int(annotated_object.y_min),
                    "x_max": int(annotated_object.x_max),
                    "y_max": int(annotated_object.y_max),
                }
                for annotated_object in self.current_objects
            ],
        }

    def _save_current_annotation(self) -> bool:
        """保存当前标注，并把样本归档到 raw 数据集目录。"""
        bin_path = self.bin_paths[self.index]
        target_class_name = self._derive_target_class_name()
        active_session_name = self._derive_target_session_name(bin_path)

        if not self.current_objects:
            messagebox.showerror("保存失败", "当前样本还没有标注框。")
            return False

        if target_class_name is None:
            messagebox.showerror("保存失败", "当前样本还没有设置目标类别。")
            return False

        if target_class_name not in self.class_names:
            messagebox.showerror("保存失败", f"不支持的目标类别：{target_class_name}")
            return False

        object_class_names = {annotated_object.class_name for annotated_object in self.current_objects}
        if target_class_name not in object_class_names:
            messagebox.showerror(
                "保存失败",
                f"归档类别 {self._display_class_name(target_class_name)} 没有出现在当前标注框里。",
            )
            return False

        target_bin_path = self._build_target_bin_path(bin_path, target_class_name, active_session_name)
        annotation_path = annotation_path_for_bin(target_bin_path)

        try:
            self._move_sample_artifacts(bin_path, target_bin_path)
        except FileExistsError as exc:
            messagebox.showerror("保存失败", str(exc))
            return False
        except OSError as exc:
            messagebox.showerror("保存失败", f"保存样本文件失败：{exc}")
            return False

        self.bin_paths[self.index] = target_bin_path
        save_json(annotation_path, self._build_annotation_payload())
        status_message = f"已保存到：{self._relative_display_path(target_bin_path)}"
        if self.auto_session_prefix is not None and self.current_session_name == active_session_name:
            self.auto_session_saved_count += 1
            if self.auto_samples_per_session > 0 and self.auto_session_saved_count >= self.auto_samples_per_session:
                next_session_name = self._advance_auto_session()
                status_message += f" | 当前分组已满，下一组：{next_session_name}"

        self.session_var.set(self._current_session_display_text(target_bin_path))
        self._update_status(status_message)
        return True

    def save_and_next(self) -> None:
        """保存当前标注并切到下一张。"""
        if self._save_current_annotation():
            self.next_sample()

    def delete_last_object(self) -> None:
        """删除最后一个标注框。"""
        if not self.current_objects:
            self._update_status("当前没有可删除的标注框")
            return
        removed = self.current_objects.pop()
        self._refresh_canvas()
        self._update_status(f"已删除最后一个标注框：{self._display_class_name(removed.class_name)}")

    def clear_objects(self) -> None:
        """清空当前样本的全部标注框。"""
        self.current_objects = []
        self.drag_start_xy = None
        self.drag_current_xy = None
        self._refresh_canvas()
        self._update_status("当前样本的标注框已清空")

    def next_sample(self) -> None:
        """切到下一张样本。"""
        if self.index >= len(self.bin_paths) - 1:
            self._update_status("已经是最后一张了")
            return
        self.index += 1
        self._load_current_sample()

    def previous_sample(self) -> None:
        """回到上一张样本。"""
        if self.index <= 0:
            self._update_status("已经是第一张了")
            return
        self.index -= 1
        self._load_current_sample()


def is_sample_already_annotated(bin_path: Path) -> bool:
    """Return whether one sample already has a usable sidecar annotation."""
    try:
        annotation_payload = load_sidecar_annotation(bin_path)
    except (OSError, ValueError):
        return False

    annotation = normalize_sidecar_annotation(annotation_payload)
    if annotation is None:
        return False
    return bool(annotation.objects)


def collect_bin_paths(input_path: Path) -> list[Path]:
    """从文件或目录中收集 .bin 文件。"""
    if input_path.is_file():
        if input_path.suffix.lower() != ".bin":
            raise SystemExit(f"输入文件不是 .bin：{input_path}")
        return [input_path]

    if input_path.is_dir():
        bin_paths = sorted(path for path in input_path.rglob("*.bin") if path.is_file())
        if not bin_paths:
            raise SystemExit(f"在目录下没有找到 .bin 文件：{input_path}")
        return bin_paths

    raise SystemExit(f"输入路径不存在：{input_path}")


def build_arg_parser() -> argparse.ArgumentParser:
    """构建命令行参数。"""
    parser = argparse.ArgumentParser(description="热像 .bin 框标注工具")
    parser.add_argument("--config", type=Path, default=None, help="dataset_config.json 路径")
    parser.add_argument(
        "--input",
        type=Path,
        default=None,
        help="输入的 .bin 文件或目录。默认使用配置里的 raw 数据集根目录。",
    )
    parser.add_argument("--raw-root", type=Path, default=None, help="归类保存时使用的 raw 数据集根目录")
    parser.add_argument("--session-name", default=None, help="从外部目录导入样本时使用的 session 名")
    parser.add_argument("--session-prefix", default=None, help="启用自动粗分组时使用的前缀，例如 thermal_people")
    parser.add_argument(
        "--auto-samples-per-session",
        type=int,
        default=12,
        help="自动粗分组时每组最多保存多少张；传 0 表示只手动按 G 或“新分组”切换",
    )
    parser.add_argument("--scale", type=int, default=4, help="预览放大倍数")
    return parser


def main() -> int:
    """程序入口。"""
    args = build_arg_parser().parse_args()
    config = load_dataset_config(args.config)

    input_path = args.input if args.input is not None else resolve_workspace_path(config["dataset_paths"]["raw_root"])
    raw_root = args.raw_root if args.raw_root is not None else resolve_workspace_path(config["dataset_paths"]["raw_root"])
    bin_paths, startup_message = collect_pending_bin_paths(input_path)

    root = tk.Tk()
    AnnotationTool(
        root=root,
        bin_paths=bin_paths,
        config=config,
        scale=args.scale,
        raw_root=raw_root,
        input_root=input_path if input_path.exists() else None,
        session_name=args.session_name,
        session_prefix=args.session_prefix,
        auto_samples_per_session=args.auto_samples_per_session,
        startup_message=startup_message,
    )
    root.mainloop()
    return 0


def collect_pending_bin_paths(input_path: Path) -> tuple[list[Path], str | None]:
    """Collect samples and skip already-annotated directory entries by default."""
    all_bin_paths = collect_bin_paths(input_path)
    if input_path.is_file():
        if is_sample_already_annotated(all_bin_paths[0]):
            return all_bin_paths, "褰撳墠鍗曟牱鏈凡鏈夋爣娉紝宸茬洿鎺ヨ浇鍏ヤ緵浣犲洖鐪嬫垨淇敼"
        return all_bin_paths, None

    selected_bin_paths = [bin_path for bin_path in all_bin_paths if not is_sample_already_annotated(bin_path)]
    skipped_count = len(all_bin_paths) - len(selected_bin_paths)
    if not selected_bin_paths:
        raise SystemExit(
            f"鐩綍涓嬬殑 {len(all_bin_paths)} 寮?.bin 鏍锋湰閮藉凡鏈夋爣娉ㄣ€? "
            "濡傛灉闇€瑕佸洖鐪嬫棫鏍囨敞锛岃鐩存帴鎸囧畾鍗曚釜 .bin 鏂囦欢浣滀负 --input"
        )
    if skipped_count > 0:
        return selected_bin_paths, f"鏈鍙姞杞芥湭鏍囨敞鏍锋湰锛?{len(selected_bin_paths)}/{len(all_bin_paths)} 寮?.bin锛岃烦杩?{skipped_count} 寮犲凡瀹屾垚"
    return selected_bin_paths, None


if __name__ == "__main__":
    raise SystemExit(main())
