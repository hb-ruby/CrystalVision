#!/usr/bin/env bash
# Download the NudeNet v3.4 640m ONNX detector into models/ and verify its checksum.
set -euo pipefail

NAME="640m.onnx"
URL="https://github.com/notAI-tech/NudeNet/releases/download/v3.4-weights/$NAME"
SHA256="04fe3d77980780c1f8297dc6d7f942fd5b3abe6942a188f742a85241e4f634eb"
DEST="$(cd "$(dirname "$0")/.." && pwd)/models"
OUT="$DEST/$NAME"

verify() { echo "$SHA256  $1" | sha256sum --check --status; }

if [[ -f "$OUT" ]] && verify "$OUT"; then
    echo "already present: $OUT"
    exit 0
fi

echo "downloading $NAME (~100 MB) ..."
rm -f "$OUT.part"
if command -v gh >/dev/null && gh auth status >/dev/null 2>&1; then
    gh release download v3.4-weights -R notAI-tech/NudeNet -p "$NAME" -O "$OUT.part"
else
    curl -fL --retry 3 -o "$OUT.part" "$URL"
fi

if ! verify "$OUT.part"; then
    echo "error: checksum mismatch for $NAME (got a login/HTML page or a corrupt download?)" >&2
    rm -f "$OUT.part"
    exit 1
fi
mv "$OUT.part" "$OUT"
echo "ok: $OUT"
