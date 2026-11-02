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
```

`--detect` takes binary PPM images and prints each detection with its label, score, box,
and timings. The output image is optional. Use a release build to get realistic timings.

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
