#pragma once

// The tiny test harness every test uses: CHECK records a failure and carries on, and main()
// ends with `return finish("name_test");`. A test that can't run here (no X display, no
// model) returns kSkip instead, which ctest reports as skipped.

#include <cstdio>

namespace check {

inline int failures = 0;
inline constexpr int kSkip = 77; // SKIP_RETURN_CODE in tests/CMakeLists.txt

// Whether resident memory says anything about leaks: under AddressSanitizer it doesn't, since
// freed memory is held in a quarantine and RSS climbs regardless (LeakSanitizer covers leaks).
#ifdef __SANITIZE_ADDRESS__
inline constexpr bool kRssIsMeaningful = false;
#else
inline constexpr bool kRssIsMeaningful = true;
#endif

inline int finish(const char* name) {
    if (failures == 0) std::printf("%s: all passed\n", name);
    return failures == 0 ? 0 : 1;
}

inline int skip(const char* name, const char* why) {
    std::printf("%s: skipped (%s)\n", name, why);
    return kSkip;
}

} // namespace check

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++check::failures;                                                                     \
        }                                                                                          \
    } while (0)
