#pragma once

#include <filesystem>
#include <optional>

namespace crystal {

// Locates the NudeNet model. Search order:
//   1. $CRYSTALVISION_MODEL (explicit override)
//   2. <exe dir>/../share/crystalvision/models/640m.onnx (installed .deb layout)
//   3. <source tree>/models/640m.onnx (development builds)
std::optional<std::filesystem::path> findModel();

} // namespace crystal
