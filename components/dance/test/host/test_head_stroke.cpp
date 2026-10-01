// SPDX-License-Identifier: BSL-1.0
#include "dance/head_stroke_detector.hpp"

#include <cstdio>
#include <limits>

using stackchan::dance::HeadStrokeDetector;
namespace {
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
void arm(HeadStrokeDetector& detector, std::uint32_t base = 0)
{
    expect(!detector.observe({0, 0, 0}, base), "quiet begins without an event");
    expect(!detector.observe({0, 0, 0}, base + 150), "quiet arms without an event");
}
void test_stroke(bool reverse, std::uint32_t base = 0)
{
    HeadStrokeDetector detector;
    arm(detector, base);
    expect(!detector.observe(reverse ? std::array<std::uint8_t, 3>{0, 0, 3}
                                    : std::array<std::uint8_t, 3>{3, 0, 0}, base + 200),
           "one zone does not trigger");
    expect(!detector.observe({0, 3, 0}, base + 250), "two zones do not trigger");
    expect(!detector.observe(reverse ? std::array<std::uint8_t, 3>{3, 0, 0}
                                    : std::array<std::uint8_t, 3>{0, 0, 3}, base + 300),
           "complete stroke waits for hand release");
    expect(!detector.observe({0, 0, 0}, base + 350), "release begins");
    expect(!detector.observe({0, 0, 0}, base + 499), "short release does not trigger");
    expect(detector.observe({0, 0, 0}, base + 500), "stroke and release trigger once");
    expect(!detector.observe({0, 0, 0}, base + 700), "quiet does not retrigger");
}
void test_noise_and_reset()
{
    HeadStrokeDetector detector;
    expect(!detector.observe({3, 0, 0}, 0), "boot with hand present ignored");
    expect(!detector.observe({0, 3, 0}, 50), "boot stroke ignored");
    expect(!detector.observe({0, 0, 3}, 100), "boot stroke cannot start");
    arm(detector, 200);
    expect(!detector.observe({3, 3, 3}, 400), "simultaneous spike rejected");
    expect(!detector.observe({0, 0, 0}, 450), "spike release ignored");
    expect(!detector.observe({0, 0, 0}, 600), "spike cannot trigger on release");
    expect(!detector.observe({2, 2, 2}, 650), "weak contacts ignored");
    expect(!detector.observe({0, 3, 0}, 700), "middle-first begins");
    expect(!detector.observe({3, 0, 0}, 750), "non-monotonic stroke continues");
    expect(!detector.observe({0, 0, 3}, 800), "non-monotonic stroke rejected");
    arm(detector, 900);
    (void)detector.observe({3, 0, 0}, 1100);
    detector.reset(); // hardware read failure / input disabled discards partial gesture
    expect(!detector.observe({0, 3, 0}, 1150), "reset discards partial stroke");
    expect(!detector.observe({0, 0, 3}, 1200), "reset cannot finish stale stroke");
    expect(!detector.observe({0, 0, 0}, 1250), "reset release begins");
    expect(!detector.observe({0, 0, 0}, 1450), "reset release cannot start");
}
void test_gap_and_held_hand()
{
    HeadStrokeDetector detector;
    arm(detector);
    (void)detector.observe({3, 0, 0}, 200);
    (void)detector.observe({0, 0, 0}, 250);
    (void)detector.observe({0, 3, 0}, 850);
    expect(!detector.observe({0, 0, 3}, 900), "old first zone expires across gap");
    detector.reset();
    arm(detector, 1000);
    (void)detector.observe({3, 0, 0}, 1200);
    (void)detector.observe({0, 3, 0}, 1250);
    (void)detector.observe({0, 0, 3}, 1300);
    expect(!detector.observe({0, 0, 3}, 3300), "held hand times out");
    expect(!detector.observe({0, 0, 0}, 3350), "late release ignored");
    expect(!detector.observe({0, 0, 0}, 3500), "timed-out gesture cannot start");
}
void test_release_bounce_and_read_failure()
{
    HeadStrokeDetector detector;
    arm(detector);
    (void)detector.observe({3, 3, 0}, 200); // adjacent zones may share a sample
    (void)detector.observe({0, 0, 3}, 250);
    (void)detector.observe({0, 0, 0}, 300);
    expect(!detector.observe({0, 0, 1}, 400), "any renewed contact cancels quiet time");
    (void)detector.observe({0, 0, 0}, 450);
    expect(!detector.observe({0, 0, 0}, 599), "quiet restarts after bounce");
    expect(detector.observe({0, 0, 0}, 600), "fresh continuous release triggers");
    arm(detector, 700);
    (void)detector.observe({3, 0, 0}, 900);
    (void)detector.observe({0, 3, 0}, 950);
    (void)detector.observe({0, 0, 3}, 1000);
    detector.reset(); // read_checked failure while waiting for hand release
    expect(!detector.observe({0, 0, 0}, 1050), "failed read discards completed stroke");
    expect(!detector.observe({0, 0, 0}, 1200), "failure cannot masquerade as release");
    (void)detector.observe({3, 0, 0}, 1250);
    expect(!detector.observe({4, 0, 0}, 1300), "out-of-range reading resets detector");
    (void)detector.observe({0, 3, 0}, 1350);
    (void)detector.observe({0, 0, 3}, 1400);
    (void)detector.observe({0, 0, 0}, 1450);
    expect(!detector.observe({0, 0, 0}, 1600), "invalid reading prevents stale completion");
}
} // namespace

int main()
{
    test_stroke(false);
    test_stroke(true);
    test_stroke(false, std::numeric_limits<std::uint32_t>::max() - 250);
    test_noise_and_reset();
    test_gap_and_held_hand();
    test_release_bounce_and_read_failure();
    if (failures != 0) return 1;
    std::puts("all head stroke detector checks passed");
    return 0;
}
