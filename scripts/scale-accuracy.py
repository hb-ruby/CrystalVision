#!/usr/bin/env python3
"""Measure how detection accuracy drops as the screen is shrunk before inference.

Simulates a desktop: each test image is pasted onto a SCREEN-sized background at several
on-screen heights, then the whole screen is shrunk by each model scale, padded only to a
multiple of 32 (rectangular input, no square letterbox), and run through the model.

The test set is a folder with up to three subfolders (searched recursively):
    nude/       should be flagged
    clean/      should not be flagged
    debatable/  reported separately, not scored

An image is "flagged" when the model finds at least one blur-worthy *_EXPOSED label
(see SCORED). Reported per on-screen height and scale:
    nude      share of images flagged (image recall), and share of the reference boxes still
              found (box recall; reference = the model's detections on the image by itself
              at NudeNet's native size, longer side 640; same label and IoU >= --iou)
    clean     share of images flagged (false positive rate)
    debatable share of images flagged

Raw detections are saved as JSON (--save) so tables can be rebuilt with --load.

Usage:
    .venv/bin/python -I scripts/scale-accuracy.py TESTSET_DIR [--screen 1920x1080]
"""

import argparse
import json
import sys
import time
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

ROOT = Path(__file__).resolve().parent.parent
LABELS = [
    "FEMALE_GENITALIA_COVERED", "FACE_FEMALE", "BUTTOCKS_EXPOSED", "FEMALE_BREAST_EXPOSED",
    "FEMALE_GENITALIA_EXPOSED", "MALE_BREAST_EXPOSED", "ANUS_EXPOSED", "FEET_EXPOSED",
    "BELLY_COVERED", "FEET_COVERED", "ARMPITS_COVERED", "ARMPITS_EXPOSED", "FACE_MALE",
    "BELLY_EXPOSED", "MALE_GENITALIA_EXPOSED", "ANUS_COVERED", "FEMALE_BREAST_COVERED",
    "BUTTOCKS_COVERED",
]
# The labels worth blurring. FEET/ARMPITS/BELLY/MALE_BREAST exposed are ordinary clothing.
SCORED = {"BUTTOCKS_EXPOSED", "FEMALE_BREAST_EXPOSED", "FEMALE_GENITALIA_EXPOSED",
          "ANUS_EXPOSED", "MALE_GENITALIA_EXPOSED"}
MIN_SCORE, NMS_IOU = 0.25, 0.45  # same as src/detector.hpp
GROUPS = ("nude", "clean", "debatable")
EXTS = {".jpg", ".jpeg", ".png", ".webp", ".ppm", ".bmp"}


def iou(a, b):
    ix = max(0.0, min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0]))
    iy = max(0.0, min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1]))
    inter = ix * iy
    uni = a[2] * a[3] + b[2] * b[3] - inter
    return inter / uni if uni > 0 else 0.0


def input_size(w, h, scale):
    sw, sh = max(1, round(w * scale)), max(1, round(h * scale))
    return sw, sh, -(-sw // 32) * 32, -(-sh // 32) * 32


def detect(session, rgb, scale):
    """Shrink by `scale`, pad right/bottom to a multiple of 32, run, return boxes in rgb coords."""
    h, w = rgb.shape[:2]
    sw, sh, pw, ph = input_size(w, h, scale)
    interp = cv2.INTER_AREA if scale < 1 else cv2.INTER_LINEAR
    canvas = np.zeros((ph, pw, 3), np.float32)
    canvas[:sh, :sw] = cv2.resize(rgb, (sw, sh), interpolation=interp).astype(np.float32) / 255.0
    x = canvas.transpose(2, 0, 1)[None]

    t = time.perf_counter()
    out = session.run(None, {session.get_inputs()[0].name: x})[0][0]  # [22, anchors]
    ms = (time.perf_counter() - t) * 1000

    cls = out[4:]
    best, score = cls.argmax(0), cls.max(0)
    keep = score >= MIN_SCORE
    fx, fy = sw / w, sh / h
    dets = sorted(
        ((LABELS[c], float(s), ((cx - bw / 2) / fx, (cy - bh / 2) / fy, bw / fx, bh / fy))
         for cx, cy, bw, bh, c, s in zip(out[0][keep], out[1][keep], out[2][keep],
                                          out[3][keep], best[keep], score[keep])),
        key=lambda d: -d[1])
    kept = []
    for d in dets:  # class-agnostic NMS, like src/detect.cpp
        if all(iou(d[2], k[2]) <= NMS_IOU for k in kept):
            kept.append(d)
    return [(lab, s, tuple(float(v) for v in b)) for lab, s, b in kept], ms


def run(args, W, H, heights, scales):
    session = ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])
    if args.background:
        bg = cv2.cvtColor(cv2.imread(str(args.background)), cv2.COLOR_BGR2RGB)
        bg = cv2.resize(bg, (W, H), interpolation=cv2.INTER_AREA)
    else:
        bg = np.full((H, W, 3), 128, np.uint8)

    files = [(g, p) for g in GROUPS if (args.testset / g).is_dir()
             for p in sorted((args.testset / g).rglob("*")) if p.suffix.lower() in EXTS]
    results = {"screen": [W, H], "heights": heights, "scales": scales, "images": [], "times": {}}
    times = {s: [] for s in scales}
    start = time.time()

    for n, (group, f) in enumerate(files, 1):
        img = cv2.imread(str(f))
        if img is None:
            print(f"skip {f}: can't read", file=sys.stderr)
            continue
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        ih, iw = img.shape[:2]
        ref, _ = detect(session, img, 640 / max(iw, ih))
        rec = {"file": str(f.relative_to(args.testset)), "group": group, "size": [iw, ih],
               "ref": ref, "screens": []}

        for th in heights:
            k = th / ih
            pw, ph = round(iw * k), th
            if pw > W:  # too wide for the screen: fit width instead
                k = W / iw
                pw, ph = W, round(ih * k)
            ox, oy = (W - pw) // 2, (H - ph) // 2
            screen = bg.copy()
            screen[oy:oy + ph, ox:ox + pw] = cv2.resize(img, (pw, ph), interpolation=cv2.INTER_AREA)
            entry = {"height": th, "place": [ox, oy, k], "dets": {}}
            for s in scales:
                dets, ms = detect(session, screen, s)
                times[s].append(ms)
                entry["dets"][str(s)] = dets
            rec["screens"].append(entry)
        results["images"].append(rec)
        el = time.time() - start
        print(f"[{n}/{len(files)}] {group}/{f.name}  ({el / n * (len(files) - n) / 60:.0f} min left)",
              file=sys.stderr, flush=True)

    results["times"] = {str(s): float(np.median(t)) for s, t in times.items() if t}
    return results


def report(res, iou_min):
    W, H = res["screen"]
    heights, scales = res["heights"], res["scales"]
    imgs = res["images"]
    by = {g: [r for r in imgs if r["group"] == g] for g in GROUPS}
    flagged = lambda dets: any(d[0] in SCORED for d in dets)

    head = "on-screen height".ljust(18) + "".join(f"{s:>8}" for s in scales)
    nude = by["nude"]
    print(f"\nScreen {W}x{H}; " + ", ".join(f"{len(v)} {g}" for g, v in by.items() if v))
    if nude:
        base = sum(flagged(r["ref"]) for r in nude)
        print(f"Nude images flagged on their own at native size (longer side 640): "
              f"{base}/{len(nude)} = {100 * base / len(nude):.0f}%")

    def table(title, group, cell):
        rs = by[group]
        if not rs:
            return
        print(f"\n{title}\n{head}")
        for i, th in enumerate(heights):
            print(f"{th:>5} px".ljust(18) + "".join(f"{cell(rs, i, str(s)):>8}" for s in scales))

    def rate(rs, i, s):
        return f"{100 * sum(flagged(r['screens'][i]['dets'][s]) for r in rs) / len(rs):.0f}%"

    def box_recall(rs, i, s):
        want = found = 0
        for r in rs:
            e = r["screens"][i]
            ox, oy, k = e["place"]
            got = [d for d in e["dets"][s] if d[0] in SCORED]
            used = set()
            for lab, _, b in r["ref"]:
                if lab not in SCORED:
                    continue
                want += 1
                box = (ox + b[0] * k, oy + b[1] * k, b[2] * k, b[3] * k)
                for j, (glab, _, gb) in enumerate(got):
                    if j not in used and glab == lab and iou(box, gb) >= iou_min:
                        used.add(j)
                        found += 1
                        break
        return f"{100 * found / want:.0f}%" if want else "-"

    print("\nColumns are model scales (screen shrunk by this factor before inference).")
    table("NUDE: images flagged (image recall, higher is better)", "nude", rate)
    table(f"NUDE: reference boxes still found (box recall, same label, IoU >= {iou_min})",
          "nude", box_recall)
    table("CLEAN: images flagged (false positives, lower is better)", "clean", rate)
    table("DEBATABLE: images flagged", "debatable", rate)

    print("\nModel input and median inference time per scale:")
    for s in scales:
        _, _, pw, ph = input_size(W, H, s)
        print(f"  {s:<6} {pw}x{ph:<6} {res['times'].get(str(s), float('nan')):6.0f} ms")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("testset", type=Path, nargs="?")
    ap.add_argument("--screen", default="1920x1080")
    ap.add_argument("--model", type=Path, default=ROOT / "models/640m.onnx")
    ap.add_argument("--background", type=Path, help="screen background image (default: mid grey)")
    ap.add_argument("--heights", default="1080,720,480,320,200,120", help="on-screen image heights, px")
    ap.add_argument("--scales", default="1.0,0.75,0.5,0.4,0.333,0.25")
    ap.add_argument("--iou", type=float, default=0.3)
    ap.add_argument("--save", type=Path, help="write raw results as JSON")
    ap.add_argument("--load", type=Path, help="report from a saved JSON instead of running")
    args = ap.parse_args()

    if args.load:
        res = json.loads(args.load.read_text())
    else:
        if not args.testset:
            ap.error("TESTSET_DIR is required unless --load is given")
        W, H = map(int, args.screen.split("x"))
        heights = [int(v) for v in args.heights.split(",") if int(v) <= H]
        scales = [float(v) for v in args.scales.split(",")]
        res = run(args, W, H, heights, scales)
        if args.save:
            args.save.write_text(json.dumps(res))
    report(res, args.iou)


if __name__ == "__main__":
    main()
