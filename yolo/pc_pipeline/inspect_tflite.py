#!/usr/bin/env python3
"""Print TFLite I/O types, shapes, quantization parameters, size and hash."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import tensorflow as tf


def tensor_summary(detail: dict) -> dict:
    quantization_parameters = detail["quantization_parameters"]
    return {
        "name": detail["name"],
        "shape": detail["shape"].tolist(),
        "dtype": str(detail["dtype"].__name__),
        "quantization": [float(detail["quantization"][0]), int(detail["quantization"][1])],
        "scales": quantization_parameters["scales"].astype(float).tolist(),
        "zero_points": quantization_parameters["zero_points"].astype(int).tolist(),
        "quantized_dimension": int(quantization_parameters["quantized_dimension"]),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    reports = []
    for model_path in args.models:
        model_path = model_path.resolve()
        data = model_path.read_bytes()
        interpreter = tf.lite.Interpreter(model_path=str(model_path))
        interpreter.allocate_tensors()
        reports.append(
            {
                "path": str(model_path),
                "size_bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "inputs": [tensor_summary(item) for item in interpreter.get_input_details()],
                "outputs": [tensor_summary(item) for item in interpreter.get_output_details()],
                "tensor_count": len(interpreter.get_tensor_details()),
            }
        )

    text = json.dumps(reports, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.resolve().write_text(text, encoding="utf-8")
        print(f"Wrote: {args.output.resolve()}")
    print(text, end="")


if __name__ == "__main__":
    main()
