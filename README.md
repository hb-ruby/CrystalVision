# CrystalVision

Desktop nudity detection for X11, built on the NudeNet v3 ONNX detector.

- **Language:** C++20
- **Inference:** ONNX Runtime (C++ API)
- **X11:** XCB (MIT-SHM capture, Composite automatic redirection so it coexists with the compositor)

## Setup (Debian 13)

```sh
sudo apt install build-essential cmake ninja-build pkgconf clang-format \
    libonnxruntime-dev \
    libxcb1-dev libxcb-shm0-dev libxcb-composite0-dev \
    libxcb-xfixes0-dev libxcb-shape0-dev libxcb-image0-dev

scripts/fetch-models.sh          # downloads 640m.onnx (~100 MB) into models/
```

To use an upstream ONNX Runtime release instead of the distro package, add
`-DONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-X.Y.Z` when configuring.

## Build

```sh
cmake --preset debug && cmake --build --preset debug
./build/debug/crystalvision
```

Presets: `debug`, `asan` (AddressSanitizer + UBSan), `release` (RelWithDebInfo).

## License

CrystalVision is licensed under the GNU AGPL v3 (see `LICENSE`), matching the
AGPL-3.0 license of the NudeNet v3 model it bundles.
