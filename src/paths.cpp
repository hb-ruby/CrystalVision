#include "paths.hpp"

#include <cstdlib>
#include <system_error>

namespace crystal {

namespace fs = std::filesystem;

namespace {
constexpr const char* kModelFile = "640m.onnx";

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
} // namespace

std::optional<fs::path> findModel() {
    if (const char* env = std::getenv("CRYSTALVISION_MODEL"); env && *env) {
        if (isFile(env)) return fs::path{env};
        return std::nullopt; // explicit override that doesn't exist: don't silently fall back
    }

    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        fs::path installed = exe.parent_path() / ".." / "share" / "crystalvision" / "models" / kModelFile;
        if (isFile(installed)) return fs::weakly_canonical(installed, ec);
    }

#ifdef CV_DEV_MODEL_DIR
    if (fs::path dev = fs::path{CV_DEV_MODEL_DIR} / kModelFile; isFile(dev)) return dev;
#endif

    return std::nullopt;
}

} // namespace crystal
