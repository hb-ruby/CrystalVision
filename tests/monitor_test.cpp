// Unit tests for the monitoring loop's scale schedule. No model or X server needed.

#include "monitor.hpp"

#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

void followsTickSchedule() {
    using crystal::tickScale;
    for (int n : {1, 2, 3, 4, 6, 7, 8, 9, 11, 14, 16, 19}) CHECK(tickScale(n) == 0.333f);
    for (int n : {5, 15, 25, 35}) CHECK(tickScale(n) == 0.5f);
    for (int n : {10, 20, 30, 100}) CHECK(tickScale(n) == 1.0f);
}

} // namespace

int main() {
    followsTickSchedule();
    if (failures == 0) std::printf("monitor_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
