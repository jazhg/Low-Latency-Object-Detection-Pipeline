#!/usr/bin/env python3
"""Export a pinned torchvision detector as a fixed-contract TorchScript module."""

from __future__ import annotations

import argparse
import array
import hashlib
import json
from pathlib import Path

import torch
import torchvision
from torch import Tensor, nn
from torchvision.models.detection import SSDLite320_MobileNet_V3_Large_Weights
from torchvision.models.detection import ssdlite320_mobilenet_v3_large
from torchvision.io import read_image
from torchvision.transforms.functional import resize


class DetectionWrapper(nn.Module):
    """Expose one 320x320 RGB float tensor as an N x 6 detection tensor."""

    def __init__(self, score_threshold: float) -> None:
        super().__init__()
        self.detector = ssdlite320_mobilenet_v3_large(
            weights=SSDLite320_MobileNet_V3_Large_Weights.DEFAULT,
            score_thresh=score_threshold,
        ).eval()

    def forward(self, image: Tensor) -> Tensor:
        if torch.jit.is_scripting():
            prediction = self.detector([image])[1][0]
        else:
            prediction = self.detector([image])[0]
        boxes = prediction["boxes"]
        scores = prediction["scores"].unsqueeze(1)
        labels = prediction["labels"].to(dtype=boxes.dtype).unsqueeze(1)
        return torch.cat((boxes, scores, labels), dim=1)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def export(
    output: Path,
    manifest_path: Path,
    fixture_image: Path,
    fixture_tensor: Path,
    score_threshold: float,
) -> None:
    torch.manual_seed(0)
    model = DetectionWrapper(score_threshold)
    scripted = torch.jit.script(model)

    sample = resize(read_image(str(fixture_image)), [320, 320], antialias=True)
    sample = sample.to(dtype=torch.float32).div(255.0).contiguous()
    with torch.inference_mode():
        eager_output = model(sample)
        scripted_output = scripted(sample)
    torch.testing.assert_close(scripted_output, eager_output, rtol=1e-5, atol=1e-5)

    output.parent.mkdir(parents=True, exist_ok=True)
    scripted.save(str(output))
    fixture_tensor.parent.mkdir(parents=True, exist_ok=True)
    with fixture_tensor.open("wb") as stream:
        array.array("f", sample.flatten().tolist()).tofile(stream)
    manifest = {
        "artifact": output.name,
        "artifact_sha256": sha256(output),
        "model": "ssdlite320_mobilenet_v3_large",
        "weights": "SSDLite320_MobileNet_V3_Large_Weights.DEFAULT",
        "python": f"{__import__('sys').version_info.major}.{__import__('sys').version_info.minor}",
        "torch": torch.__version__,
        "torchvision": torchvision.__version__,
        "export_warning": (
            "torch.jit.script is deprecated and is not officially supported on "
            "Python 3.14; this artifact is accepted only after the C++ parity gate"
        ),
        "input": {
            "shape": [3, 320, 320],
            "dtype": "float32",
            "range": [0.0, 1.0],
            "color_order": "RGB",
        },
        "output": {
            "shape": "N x 6",
            "columns": ["x1", "y1", "x2", "y2", "score", "class_id"],
            "coordinates": "input pixels",
            "nms": "included by torchvision detector",
        },
        "score_threshold": score_threshold,
        "parity_fixture": fixture_image.as_posix(),
        "parity_tensor": fixture_tensor.as_posix(),
        "parity_tensor_sha256": sha256(fixture_tensor),
        "parity_rows": int(scripted_output.shape[0]),
        "parity_sum": float(scripted_output.sum()),
        "parity_first_row": (
            scripted_output[0].tolist() if scripted_output.shape[0] > 0 else []
        ),
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("models/ssdlite320.pt"))
    parser.add_argument(
        "--manifest", type=Path, default=Path("models/ssdlite320.manifest.json")
    )
    parser.add_argument("--score-threshold", type=float, default=0.25)
    parser.add_argument(
        "--fixture-image", type=Path, default=Path("models/fixtures/dog.jpg")
    )
    parser.add_argument(
        "--fixture-tensor", type=Path, default=Path("models/fixtures/dog.f32")
    )
    args = parser.parse_args()
    if not 0.0 <= args.score_threshold <= 1.0:
        parser.error("--score-threshold must be in [0, 1]")
    if not args.fixture_image.is_file():
        parser.error(f"fixture image does not exist: {args.fixture_image}")
    export(
        args.output,
        args.manifest,
        args.fixture_image,
        args.fixture_tensor,
        args.score_threshold,
    )


if __name__ == "__main__":
    main()
