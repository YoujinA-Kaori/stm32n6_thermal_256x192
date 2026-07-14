#!/usr/bin/env python3
"""Re-quantize the exported YOLO ONNX model with per-channel INT8 weights."""

from __future__ import annotations

import argparse
import json
import shutil
import zipfile
from pathlib import Path

import numpy as np
import onnx
import onnx2tf
import torch
import yaml
from ultralytics.data.build import build_dataloader
from ultralytics.data.dataset import YOLODataset
from ultralytics.data.utils import check_det_dataset


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--onnx",
        type=Path,
        default=(
            script_dir.parent
            / "pc_runs"
            / "common9_yolov8n_320_w0"
            / "weights"
            / "best.onnx"
        ),
    )
    parser.add_argument(
        "--data",
        type=Path,
        default=script_dir / "calibration_dataset.yaml",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=(
            script_dir.parent
            / "pc_runs"
            / "common9_yolov8n_320_w0"
            / "weights"
            / "best_saved_model_per_channel"
        ),
    )
    parser.add_argument(
        "--metadata",
        type=Path,
        default=(
            script_dir.parent
            / "pc_runs"
            / "common9_yolov8n_320_w0"
            / "weights"
            / "best_saved_model"
            / "metadata.yaml"
        ),
        help="Ultralytics metadata YAML embedded into exported TFLite files.",
    )
    parser.add_argument(
        "--metadata-only",
        action="store_true",
        help="Only embed metadata into existing TFLite files in --output.",
    )
    parser.add_argument("--imgsz", type=int, default=320)
    parser.add_argument("--batch", type=int, default=1)
    return parser.parse_args()


def build_calibration_array(data_yaml: Path, imgsz: int, batch_size: int) -> np.ndarray:
    data = check_det_dataset(str(data_yaml))
    dataset = YOLODataset(
        data["val"],
        data=data,
        fraction=1.0,
        task="detect",
        imgsz=imgsz,
        augment=False,
        batch_size=batch_size,
    )
    dataloader = build_dataloader(dataset, batch=batch_size, workers=0, drop_last=True)
    images = [batch["img"] for batch in dataloader]
    tensor = torch.nn.functional.interpolate(
        torch.cat(images, 0).float(), size=(imgsz, imgsz)
    ).permute(0, 2, 3, 1)
    return tensor.numpy().astype(np.float32)


def load_metadata(metadata_path: Path) -> dict:
    if not metadata_path.is_file():
        raise FileNotFoundError(f"Metadata YAML not found: {metadata_path}")
    metadata = yaml.safe_load(metadata_path.read_text(encoding="utf-8"))
    if not isinstance(metadata, dict):
        raise ValueError(f"Expected a metadata mapping in: {metadata_path}")
    return metadata


def embed_tflite_metadata(tflite_path: Path, metadata: dict) -> None:
    """Append Ultralytics metadata without changing the FlatBuffer model."""
    with zipfile.ZipFile(tflite_path, "a") as archive:
        if "metadata.json" in archive.namelist():
            print(f"Metadata already present, skipped: {tflite_path.name}")
            return
        archive.writestr(
            "metadata.json",
            json.dumps(metadata, ensure_ascii=False, indent=2),
        )
    print(f"Embedded metadata: {tflite_path.name}")


def main() -> None:
    args = parse_args()
    onnx_path = args.onnx.resolve()
    data_yaml = args.data.resolve()
    output_dir = args.output.resolve()
    metadata_path = args.metadata.resolve()
    metadata = load_metadata(metadata_path)

    if args.metadata_only:
        tflite_paths = sorted(output_dir.glob("*.tflite"))
        if not tflite_paths:
            raise FileNotFoundError(f"No TFLite files found in: {output_dir}")
        for tflite_path in tflite_paths:
            embed_tflite_metadata(tflite_path, metadata)
        return

    if not onnx_path.is_file():
        raise FileNotFoundError(f"ONNX model not found: {onnx_path}")
    if not data_yaml.is_file():
        raise FileNotFoundError(f"Calibration YAML not found: {data_yaml}")

    model = onnx.load(str(onnx_path), load_external_data=False)
    if len(model.graph.input) != 1:
        raise ValueError(f"Expected one ONNX input, found {len(model.graph.input)}")
    input_name = model.graph.input[0].name

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)
    calibration_path = output_dir / "calibration_images_bhwc_float32.npy"
    calibration = build_calibration_array(data_yaml, args.imgsz, args.batch)
    np.save(calibration_path, calibration)

    manifest = {
        "onnx": str(onnx_path),
        "data": str(data_yaml),
        "input_name": input_name,
        "image_count": int(calibration.shape[0]),
        "shape": list(calibration.shape),
        "dtype": str(calibration.dtype),
        "minimum": float(calibration.min()),
        "maximum": float(calibration.max()),
        "quant_type": "per-channel",
        "metadata": str(metadata_path),
    }
    (output_dir / "calibration_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))

    onnx2tf.convert(
        input_onnx_file_path=str(onnx_path),
        output_folder_path=str(output_dir),
        not_use_onnxsim=True,
        verbosity="error",
        output_integer_quantized_tflite=True,
        quant_type="per-channel",
        custom_input_op_name_np_data_path=[
            [input_name, calibration_path, [[[[0, 0, 0]]]], [[[[255, 255, 255]]]]]
        ],
        input_quant_dtype="int8",
        output_quant_dtype="int8",
        enable_batchmatmul_unfold=True,
        output_signaturedefs=True,
        optimization_for_gpu_delegate=True,
    )
    for tflite_path in sorted(output_dir.glob("*.tflite")):
        embed_tflite_metadata(tflite_path, metadata)
    calibration_path.unlink()
    print(f"Removed temporary calibration array: {calibration_path}")
    print(f"Per-channel TFLite export complete: {output_dir}")


if __name__ == "__main__":
    main()
