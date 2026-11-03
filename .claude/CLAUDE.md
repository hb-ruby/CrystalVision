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

To do:
- [ ] Continuous monitoring loop: capture → detect on an interval, keeping CPU use reasonable
- [ ] Screen change detection: scan only the regions that actually changed since the last scan, each cropped and scaled as close to the model's 640px input as possible. Full-screen scans run on a much slower interval to spare CPU.
- [ ] Overlay/blur windows over detected regions (excluded from capture, see Gotchas)
- [ ] Settings: label selection, score threshold, scan interval
- [ ] Autostart on login
- [ ] First tagged release (`v0.1.0`) and install test on a clean Mint 22
- [ ] User controls: still need to figure out. Blocking should not be able to be disabled

## Working with the user
- Use plain English but keep technical terms and detail.
