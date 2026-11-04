# CrystalVision: notes for Claude

Build, packaging, and setup steps are in `README.md`. This file covers context the code doesn't show.

## What it is
An X11 desktop app that detects nudity on screen using the NudeNet v3 ONNX detector.
Stack: C++20, ONNX Runtime C++ API, XCB (MIT-SHM capture; Composite with *automatic* redirection so it doesn't fight the compositor).

## Decisions
- **Model: NudeNet v3.4 640m**
- **License: AGPL-3.0** (matches NudeNet).
- **Target: Linux Mint 22 X11** (Ubuntu 24.04 base). Non-Debian distros are out of scope.
- **Distribution: `.deb`** built by GitHub Actions on `ubuntu-24.04`, bundling upstream ONNX Runtime 1.31.0. Ubuntu 24.04 has no ORT package, and building on a newer distro would require a newer glibc than Mint has.
- Renamed from RubyVision. Any leftover "RubyVision" is a bug.

## Dev environment
- Linux Mint 22 VM with desktop.
- CPU-only inference (no usable GPU), so model cost matters.

## Gotchas
- **Capture feedback loop:** exclude the app's own overlay/blur windows from the screen capture it analyzes.
- Model weights (`models/*.onnx`) aren't committed. Run `scripts/fetch-models.sh` after cloning.
- Pushing changes to `.github/workflows/` needs a GitHub token with the `workflow` scope.

## Roadmap
Personal tracking. Tick items off as they land, and add new ones as they come up.

Done:
- [x] Screen capture via MIT-SHM, including the wallpaper under window layers (`--dump-frame`)
- [x] Single-image NudeNet inference (`--detect`, `--detect-screen`), checked against Python NudeNet
- [x] `.deb` packaging and CI build on `ubuntu-24.04`
- [x] Continuous monitoring loop (`--monitor`): 1 s gap after each tick finishes; scale 0.333, every 5th tick 0.5, every 10th 1.0; inference capped at 4 threads
- [x] Black-box overlay over `*_EXPOSED` detections: one shaped, click-through, override-redirect window, excluded from capture; sharper-scan finds held until a same-or-sharper tick clears them
- [x] Calm/Alert scan states (`Scheduler`), replacing the fixed 1 s loop: Calm 0.333 every 5 s, every 6th at 1.0; Alert (any `triggersAlert` find, score ≥ 0.3) alternates 1.0/0.5 with 0.5 s gaps; back to Calm 30 s after the last trigger once no blocks are held

To do:
- [ ] Screen change detection (Calm state only): before each 5 s Calm scan (0.333 or the every-6th 1.0), compare the captured screen with the one from the last scan. If it hasn't changed, skip inference, so an idle or static screen costs almost no CPU. Skipped scans still count toward the every-6th 1.0 scan. Alert scans always run.
  - **Grid:** split the screen into coarse cells (about 60 px, so 32×18 at 1920×1080). Store each cell's mean brightness, mean colour (chroma) and contrast (variance), sampling every 2nd–4th pixel. That's one pass over the frame, about 1–3 ms on one core, far cheaper than a 0.333 scan.
  - **Threshold:** scan if any *single* cell changed by more than a threshold. Using the largest cell change rather than the whole-screen average means a small image appearing isn't diluted by an otherwise static screen. Slightly scrolled text stays under the threshold, because each cell's average barely moves and grey text adds almost no chroma change. The value needs tuning on real screens (text PDFs, scrolling browser, thumbnail grids).
  - **Compare against the last scan that ran**, not the last capture, so slow scrolling can't creep under the threshold 5 s at a time and add up to an unscanned new page.
  - **Safety net:** force a real scan after a run of skips (at least every 60 s), so a missed change or a bad threshold can't leave the screen unscanned indefinitely.
- [ ] Settings: label selection, score thresholds (adjustable; fixed for now at 0.5 to block, `kBlockScore`, and 0.3 to enter Alert), Calm/Alert intervals and exit time
- [ ] Autostart on login
- [ ] First tagged release (`v0.1.0`) and install test on a clean Mint 22
- [ ] User controls: still need to figure out. Blocking should not be able to be disabled

## Working with the user
- Use plain English but keep technical terms and detail.
