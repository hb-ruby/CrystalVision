# CrystalVision

Desktop nudity detection for X11, built on the NudeNet v3 ONNX detector.

- **Language:** C++20
- **Inference:** ONNX Runtime (C++ API)
- **X11:** XCB (MIT-SHM capture, Composite automatic redirection so it coexists with the compositor)

## Install

Download the `.deb` from the [Releases](https://github.com/hb-ruby/CrystalVision/releases) page, then:

```sh
sudo apt install ./crystalvision_*_amd64.deb
```

Targets Linux Mint 22 (X11). It should also work on Ubuntu 24.04+ and Debian 12+.

The package installs `/etc/xdg/autostart/crystalvision.desktop`, so `crystalvision --monitor`
starts at every graphical login, for every user. Log out and back in after installing. It runs
in the user's own session (it needs their X display), and its log goes to `~/.xsession-errors`.

## Development setup

```sh
sudo apt install build-essential cmake ninja-build pkgconf dpkg-dev clang-format \
    libxcb1-dev libxcb-shm0-dev libxcb-composite0-dev \
    libxcb-xfixes0-dev libxcb-shape0-dev libxcb-image0-dev

scripts/fetch-models.sh          # downloads 640m.onnx (~100 MB) into models/
```

ONNX Runtime doesn't need installing. CMake downloads Microsoft's official prebuilt
release (pinned and hash-checked in `cmake/OnnxRuntime.cmake`), and the `.deb` bundles it.

## Build

```sh
cmake --preset debug && cmake --build --preset debug
./build/debug/crystalvision
```

Presets: `debug`, `asan` (AddressSanitizer + UBSan), `release`.

```sh
ctest --preset debug                                     # tests
ctest --preset asan                                      # the same, checking memory and UB
./build/debug/crystalvision --dump-frame frame.ppm       # capture the screen once
./build/release/crystalvision --detect in.ppm out.ppm    # detect on an image, draw boxes
./build/release/crystalvision --detect-screen out.ppm    # detect on one screen capture
./build/release/crystalvision --monitor                  # keep scanning the screen, cover finds
./build/release/crystalvision --compare a.ppm b.ppm      # how much change detection sees
```

`x11_test` (capture and overlay) needs `$DISPLAY` and `detector_test` needs the model; each is
skipped without it. `x11_test` briefly shows a small red window and a black box on the desktop.
The `asan` preset runs every test under AddressSanitizer, LeakSanitizer and UBSan, and any
finding fails the test.

`--detect` takes binary PPM images and prints each detection with its label, score, box,
and timings. The output image is optional. Use a release build to get realistic timings.

`--monitor` runs the detection loop until Ctrl+C (or SIGTERM/SIGHUP). In a terminal it logs
every tick. When stdout isn't a terminal (started at login, or redirected to a file), it logs only
the start, Calm/Alert changes and the stop; add `--verbose` to get every tick anyway. Each tick captures the whole screen,
shrinks it, and runs the model on it, then waits *after* the tick finishes before starting the
next. How much it shrinks and how long it waits depend on the scan state:

- **Calm** (the default): a 0.333 scan every 5 s, with every 6th at full resolution so small
  images (thumbnail grids) still get caught.
- **Alert**: entered when a scan finds an exposed label (see below) or a covered female
  genitalia or anus label, scoring 0.3 or more. Scans alternate 1.0 and 0.5, 0.5 s apart.
  It drops back to Calm 30 s after the last such find, but only once no boxes are held.

Calm scans are skipped when the screen hasn't changed, so an idle or static screen costs
almost no CPU. The screen is summarised in 60 px cells (mean brightness and colour, from every
3rd pixel; under 1 ms), and a scan runs if any single cell's colour moved by more than 5 or its
brightness by more than 45 (out of 255). Colour carries most of the weight: scrolling grey text
hardly changes it, while even a small colour thumbnail does. Each scan is compared with the
last scan that ran, and full-resolution scans with the last full-resolution scan, so slow
changes add up and a small image that a 0.333 scan missed still gets its sharp look. Skipped
scans count toward the every-6th full scan, and a scan always runs at least once every 2 minutes.
`--compare` prints the numbers for two screenshots, for checking the thresholds.

Unlike `--detect`, the screen isn't letterboxed into a 640x640 square. It keeps its aspect ratio and is padded
only to a multiple of 32 (the setup `scripts/scale-accuracy.py` measures).

Inference uses at most 4 threads. Detections of the explicit `*_EXPOSED` labels (buttocks,
female breast, genitalia, anus) are covered with black boxes. The boxes are a click-through
overlay window that the capture leaves out, so each scan still sees what's underneath. A box
found by a sharper scan (0.5 or 1.0) stays up until a tick at the same or a sharper scale
no longer finds it, so a cheap 0.333 tick can't uncover what only a full scan caught. When a
coarser tick finds the same area again, the held box grows to cover both finds instead of a
second box being added.

To check the detector against NudeNet's own Python code (needs `python3.12-venv`):

```sh
python3 -m venv .venv && .venv/bin/pip install -r scripts/requirements-reference.txt
.venv/bin/python scripts/compare-nudenet.py image1.ppm image2.ppm   # prints PASS or FAIL
```

The capture leaves out CrystalVision's own windows, so it can differ from a plain screenshot.

## Packaging

```sh
cmake --preset release && cmake --build --preset release
(cd build/release && cpack)      # -> build/release/crystalvision_<ver>_amd64.deb
```

Release `.deb`s are built by GitHub Actions on Ubuntu 24.04 (`.github/workflows/deb.yml`)
so they install on Linux Mint 22. A `.deb` built on a newer distro (e.g. Debian 13) won't.
To publish one, push a tag: `git tag v0.1.0 && git push --tags`.

## License

CrystalVision is licensed under the GNU AGPL v3 (see `LICENSE`), matching the
AGPL-3.0 license of the NudeNet v3 model it bundles.
