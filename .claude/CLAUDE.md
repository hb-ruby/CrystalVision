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

## Working with the user
- Use plain English but keep technical terms and detail.
