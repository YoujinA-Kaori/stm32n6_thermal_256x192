#!/usr/bin/env python3
"""Export one real thermal sample as CubeAI-friendly raw input/output files."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

import numpy as np

import _bootstrap

_bootstrap.setup_python_path()

from src.common import (
    describe_frame_normalization,
    ensure_dir,
    get_class_names,
    load_dataset_config,
    load_sidecar_annotation,
    load_temp14_frame,
    normalize_sidecar_annotation,
    normalize_temp14_frame,
    resolve_workspace_path,
    save_json,
    scan_processed_split,
    temp14_preview_u8,
)
from src.tflite_utils import (
    inspect_tflite_model,
    load_tflite_interpreter,
    quantize_input_tensor,
    run_tflite_inference,
)


def save_preview_png(frame_u16: np.ndarray, config: dict[str, Any], output_path: Path) -> None:
    """Save a grayscale preview PNG for quick visual reference."""
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required when saving preview PNGs. "
            f"Install it in the active Python environment first: {exc}"
        ) from exc

    preview_u8 = temp14_preview_u8(frame_u16, config)
    image = Image.fromarray(preview_u8, mode="L")
    image = image.resize((preview_u8.shape[1] * 2, preview_u8.shape[0] * 2), Image.NEAREST)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)


def write_raw_tensor(raw_path: Path, tensor: np.ndarray, dtype: np.dtype) -> None:
    """Write one tensor to a flat little-endian raw file."""
    output_array = np.ascontiguousarray(tensor.astype(dtype, copy=False))
    raw_path.parent.mkdir(parents=True, exist_ok=True)
    output_array.tofile(raw_path)


def write_csv_tensor(csv_path: Path, tensor: np.ndarray) -> None:
    """Write one tensor as a single flattened CSV row."""
    output_array = np.ascontiguousarray(tensor).reshape(1, -1)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    np.savetxt(csv_path, output_array, delimiter=",", fmt="%.9g")


def select_sample(args, config: dict[str, Any]):
    """Select one processed sample for export."""
    if args.input is not None:
        return {
            "class_name": args.class_name if args.class_name is not None else "unknown",
            "bin_path": args.input,
            "annotation_path": args.input.with_suffix(".json") if args.input.with_suffix(".json").exists() else None,
            "split": "direct",
            "group_key": args.input.parent.name,
        }

    processed_root = (
        args.processed_root
        if args.processed_root is not None
        else resolve_workspace_path(config["dataset_paths"]["processed_root"])
    )
    class_names = get_class_names(config)
    samples = scan_processed_split(processed_root, args.split, class_names)
    if args.class_name is not None:
        samples = [sample for sample in samples if sample.class_name == args.class_name]
    if not samples:
        raise SystemExit("no processed samples found for the requested split/class")
    if args.sample_index < 0 or args.sample_index >= len(samples):
        raise SystemExit(f"sample-index out of range: {args.sample_index}, available={len(samples)}")
    return samples[args.sample_index]


def build_arg_parser() -> argparse.ArgumentParser:
    """Build the CLI argument parser."""
    parser = argparse.ArgumentParser(description="Export one real model input/output pair for CubeAI Browse validation")
    parser.add_argument("--config", type=Path, default=None, help="Path to dataset_config.json")
    parser.add_argument("--model", type=Path, default=None, help="TFLite model path, default best_model_int8.tflite")
    parser.add_argument("--processed-root", type=Path, default=None, help="Processed dataset root, default from config")
    parser.add_argument("--split", choices=("train", "val", "test"), default="test", help="Processed split to sample from")
    parser.add_argument("--class-name", default="circuit_board_normal", help="Optional processed class filter")
    parser.add_argument("--sample-index", type=int, default=0, help="Stable index after filtering")
    parser.add_argument("--input", type=Path, default=None, help="Optional direct .bin file to export")
    parser.add_argument("--output-dir", type=Path, default=None, help="Output directory for CubeAI validation files")
    return parser


def main() -> int:
    """Program entry point."""
    args = build_arg_parser().parse_args()
    config = load_dataset_config(args.config)
    model_path = (
        args.model
        if args.model is not None
        else resolve_workspace_path(config["artifacts"]["model_dir"]) / "best_model_int8.tflite"
    )
    if not model_path.exists():
        raise SystemExit(f"TFLite model not found: {model_path}")

    sample = select_sample(args, config)
    sample_bin_path = sample.bin_path if hasattr(sample, "bin_path") else sample["bin_path"]
    sample_class_name = sample.class_name if hasattr(sample, "class_name") else sample["class_name"]
    output_dir = (
        args.output_dir
        if args.output_dir is not None
        else resolve_workspace_path(config["artifacts"]["report_dir"]) / "cubeai_inputs" / f"{model_path.stem}_{sample_class_name}_{sample_bin_path.stem}"
    )
    ensure_dir(output_dir)

    frame_u16 = load_temp14_frame(sample_bin_path, config)
    frame_input = normalize_temp14_frame(frame_u16, config, add_channel_axis=True).astype(np.float32)
    normalization = describe_frame_normalization(frame_u16, config)
    annotation = normalize_sidecar_annotation(load_sidecar_annotation(sample_bin_path))

    interpreter = load_tflite_interpreter(model_path)
    tflite_meta = inspect_tflite_model(model_path)
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    encoded_input = quantize_input_tensor(frame_input[np.newaxis, ...], input_detail)[0]
    decoded_outputs = run_tflite_inference(interpreter, frame_input)
    decoded_output = decoded_outputs[0][0]

    float_input_path = output_dir / "input_float32_nhwc.raw"
    model_input_path = output_dir / f"input_model_dtype_nhwc.{np.dtype(input_detail['dtype']).name}.raw"
    float_input_csv_path = output_dir / "input_float32_flat.csv"
    model_input_csv_path = output_dir / f"input_model_dtype_flat.{np.dtype(input_detail['dtype']).name}.csv"
    float_output_path = output_dir / "output_float32_ghwc.raw"
    preview_path = output_dir / "preview.png"
    guide_path = output_dir / "how_to_use_in_cubeai.txt"

    write_raw_tensor(float_input_path, frame_input, np.dtype("<f4"))
    write_raw_tensor(model_input_path, encoded_input, np.dtype(input_detail["dtype"]))
    write_csv_tensor(float_input_csv_path, frame_input)
    write_csv_tensor(model_input_csv_path, encoded_input)
    write_raw_tensor(float_output_path, decoded_output, np.dtype("<f4"))
    save_preview_png(frame_u16, config, preview_path)

    guide_lines = [
        "CubeAI validation input export",
        "",
        f"Model: {model_path}",
        f"Source sample: {sample_bin_path}",
        f"Recommended input file for Browse: {model_input_csv_path.name}",
        f"Model input dtype: {np.dtype(input_detail['dtype']).name}",
        f"Model input shape: {list(input_detail['shape'])}",
        f"Model output shape: {list(output_detail['shape'])}",
        "",
        "Suggested CubeAI selections:",
        "1. Validation inputs -> Browse",
        f"2. Select: {model_input_csv_path.name}",
        "3. Validation outputs can stay None for a quick check",
        "",
        "Reference files in this folder:",
        f"- {float_input_path.name}: normalized float32 NHWC input before quantization",
        f"- {model_input_path.name}: quantized model input in the model dtype",
        f"- {float_input_csv_path.name}: normalized float32 input flattened as one CSV row",
        f"- {model_input_csv_path.name}: quantized model input flattened as one CSV row",
        f"- {float_output_path.name}: dequantized float32 model output tensor",
        f"- {preview_path.name}: display-only thermal preview of the same source frame",
    ]
    guide_path.write_text("\n".join(guide_lines) + "\n", encoding="utf-8")

    metadata = {
        "model": str(model_path),
        "source_sample": str(sample_bin_path),
        "class_name": sample_class_name,
        "input_float32_file": str(float_input_path),
        "input_model_dtype_file": str(model_input_path),
        "input_float32_csv_file": str(float_input_csv_path),
        "input_model_dtype_csv_file": str(model_input_csv_path),
        "output_float32_file": str(float_output_path),
        "preview_file": str(preview_path),
        "input_shape_nhwc": list(frame_input.shape),
        "model_input_detail": {
            "name": input_detail["name"],
            "shape": [int(value) for value in input_detail["shape"]],
            "dtype": np.dtype(input_detail["dtype"]).name,
            "quantization": {
                "scale": float(input_detail.get("quantization", (0.0, 0))[0]),
                "zero_point": int(input_detail.get("quantization", (0.0, 0))[1]),
            },
        },
        "model_output_detail": {
            "name": output_detail["name"],
            "shape": [int(value) for value in output_detail["shape"]],
            "dtype": np.dtype(output_detail["dtype"]).name,
            "quantization": {
                "scale": float(output_detail.get("quantization", (0.0, 0))[0]),
                "zero_point": int(output_detail.get("quantization", (0.0, 0))[1]),
            },
        },
        "normalization": normalization,
        "annotation": {
            "primary_class_name": annotation.primary_class_name if annotation is not None else None,
            "is_empty": annotation.is_empty if annotation is not None else None,
            "object_count": len(annotation.objects) if annotation is not None else 0,
        },
        "tflite_io": tflite_meta,
    }
    save_json(output_dir / "metadata.json", metadata)

    print(f"CubeAI input package written to {output_dir}")
    print(f"Recommended input file: {model_input_csv_path}")
    print(f"Preview image:          {preview_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
