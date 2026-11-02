# CrystalVision: notes for Claude

Build, packaging, and setup steps are in `README.md`. This file records the context and decisions that the code doesn't show.

## What it is
A desktop app that watches the user's X11 screen and detects nudity using the NudeNet v3 ONNX detector.
The stack is C++20, the ONNX Runtime C++ API, and XCB (MIT-SHM capture, plus Composite with *automatic* redirection so it doesn't fight the compositor).

## Decisions (settled, don't revisit)
- **Model: NudeNet v3.4 `640m`** is final. Don't benchmark it against `320n`.
- **License: AGPL-3.0**, matching NudeNet's license.
- **Target: Linux Mint 22 X11** (Ubuntu 24.04 base). Non-Debian distros are out of scope.
- **Distribution: a `.deb`** built by GitHub Actions on `ubuntu-24.04`, with upstream ONNX Runtime 1.31.0 bundled.
  The reasons: Ubuntu 24.04 has no ORT package, and a .deb built on a newer distro (e.g. Debian 13) needs a newer glibc than Mint has.
- The project was renamed from RubyVision (to avoid confusion with the Ruby language). Treat any leftover "RubyVision" as a bug.

## Dev environment
- Development started on a headless Debian 13 VM (no X server). It's moving to a **Linux Mint 22 VM with a desktop** so visual/X11 testing can happen.
- Inference runs CPU-only (no usable GPU), so model cost matters.
- On a headless machine, X features need `Xvfb`.

## Gotchas
- **Capture feedback loop:** the app's own overlay/blur windows must not end up in the screen capture it analyzes. Exclude them from capture.
- Model weights (`models/*.onnx`) are not committed. Run `scripts/fetch-models.sh` after cloning.
- Pushing changes to `.github/workflows/` needs a GitHub token with the `workflow` scope.

## Working with the user
- Explain concepts quickly and in plain English. Lead with the practical consequence, then the fix. Keep the technical detail for code and commit messages.
