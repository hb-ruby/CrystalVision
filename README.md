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
ctest --preset debug                                     # unit tests
./build/debug/crystalvision --dump-frame frame.ppm       # capture the screen once
./build/release/crystalvision --detect in.ppm out.ppm    # detect on an image, draw boxes
./build/release/crystalvision --detect-screen out.ppm    # detect on one screen capture
./build/release/crystalvision --monitor                  # capture + detect every second
```

`--detect` takes binary PPM images and prints each detection with its label, score, box,
and timings. The output image is optional. Use a release build to get realistic timings.

`--monitor` runs the detection loop until Ctrl+C. Each tick captures the whole screen,
shrinks it, and runs the model on it, then waits one second *after* the tick finishes before
starting the next. The shrink factor cycles so that most ticks are cheap: every 10th tick runs
at full resolution, every other 5th tick at 0.5, and the rest at 0.333. Unlike `--detect`,
the screen isn't letterboxed into a 640x640 square. It keeps its aspect ratio and is padded
only to a multiple of 32 (the setup `scripts/scale-accuracy.py` measures).

Inference uses at most 4 threads. Detections of the explicit `*_EXPOSED` labels (buttocks,
female breast, genitalia, anus) are covered with black boxes. The boxes are a click-through
overlay window that the capture leaves out, so each scan still sees what's underneath. A box
found by a sharper scan (0.5 or 1.0) stays up until a tick at the same or a sharper scale
no longer finds it, so a cheap 0.333 tick can't uncover what only a full scan caught.

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
