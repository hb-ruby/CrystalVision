#!/usr/bin/env python3
"""Check that CrystalVision's detector gives the same results as NudeNet's own Python code.

Runs the reference `nudenet` package and `crystalvision --detect` on the same images with
the same model, and matches the detections one to one. Both use ONNX Runtime and the same
.onnx file, so any real difference comes from our pre- or post-processing.

Setup (once):
    python3 -m venv .venv && .venv/bin/pip install -r scripts/requirements-reference.txt

Usage:
    .venv/bin/python scripts/compare-nudenet.py IMAGE.ppm [IMAGE.ppm ...]

Use PPM images so both sides decode exactly the same pixels. Exits 1 on any mismatch.

Channel order: nudenet.py (3.4.2) feeds the model BGR by accident. It calls
cvtColor(COLOR_RGBA2BGR) on OpenCV's 3-channel BGR image, which swaps it to RGB, then
blobFromImage(swapRB=True) swaps it back. The model was trained (Ultralytics YOLOv8) on
RGB, so CrystalVision feeds RGB. To compare everything else exactly, the reference is
given the image with red and blue pre-swapped, which makes its input RGB too.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCORE_TOL = 0.04  # engine builds and float rounding differ slightly
BOX_TOL = 10  # pixels: NudeNet truncates boxes to ints; OpenCV resizes with fixed point

LINE = re.compile(r"^\s+(\w+)\s+([\d.]+)\s+x=(-?\d+) y=(-?\d+) w=(-?\d+) h=(-?\d+)$")


def reference(image, model):
    import cv2  # imported here so --help works without the venv
    import numpy as np
    from nudenet import NudeDetector

    detector = reference.cache.get(model)
    if detector is None:
        detector = reference.cache[model] = NudeDetector(
            model_path=str(model), inference_resolution=640
        )
    rgb = np.ascontiguousarray(cv2.imread(str(image))[..., ::-1])  # see "Channel order"
    return [(d["class"], d["score"], *d["box"]) for d in detector.detect(rgb)]


reference.cache = {}


def ours(image, binary, model):
    env = {"CRYSTALVISION_MODEL": str(model), "PATH": "/usr/bin:/bin"}
    out = subprocess.run(
        [str(binary), "--detect", str(image)], capture_output=True, text=True, env=env
    )
    if out.returncode != 0:
        sys.exit(f"error: {binary} failed:\n{out.stderr}")
    dets = []
    for line in out.stdout.splitlines():
        if m := LINE.match(line):
            label, score, *box = m.groups()
            dets.append((label, float(score), *map(int, box)))
    return dets


def fmt(d):
    return f"{d[0]:<26} {d[1]:.3f}  x={d[2]} y={d[3]} w={d[4]} h={d[5]}"


def matches(a, b):
    return (
        a[0] == b[0]
        and abs(a[1] - b[1]) <= SCORE_TOL
        and all(abs(p - q) <= BOX_TOL for p, q in zip(a[2:], b[2:]))
    )


def compare(image, binary, model):
    ref = reference(image, model)
    got = ours(image, binary, model)
    print(f"{image.name}: reference {len(ref)}, ours {len(got)}")

    ok = True
    unmatched = list(got)
    for r in ref:
        hit = next((g for g in unmatched if matches(r, g)), None)
        if hit:
            unmatched.remove(hit)
            print(f"  ok    {fmt(r)}")
            print(f"        {'':<26} {hit[1]:.3f}  x={hit[2]} y={hit[3]} w={hit[4]} h={hit[5]}")
        else:
            ok = False
            print(f"  MISS  {fmt(r)}   (reference only)")
    for g in unmatched:
        ok = False
        print(f"  EXTRA {fmt(g)}   (ours only)")
    return ok


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("images", nargs="+", type=Path)
    p.add_argument("--bin", type=Path, default=ROOT / "build/release/crystalvision")
    p.add_argument("--model", type=Path, default=ROOT / "models/640m.onnx")
    args = p.parse_args()

    for path in (args.bin, args.model, *args.images):
        if not path.is_file():
            sys.exit(f"error: not found: {path}")

    results = [compare(img, args.bin, args.model) for img in args.images]
    print("PASS" if all(results) else "FAIL")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
