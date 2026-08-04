// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include <array>
#include <cstdint>

#include "clap_dance/clap_dance.hpp"
#include "test_support.hpp"

using namespace stackchan::clap_dance;

namespace {

constexpr std::size_t kFrameSamples = 160;

std::array<std::int16_t, kFrameSamples> impulse(std::int16_t dc,
                                                std::int16_t peak)
{
    std::array<std::int16_t, kFrameSamples> frame{};
    frame.fill(dc);
    frame[kFrameSamples / 2] = peak;
    return frame;
}

} // namespace

int main()
{
    ClapDetector detector;
    std::array<std::int16_t, kFrameSamples> dc_only{};
    dc_only.fill(5'000);
    for (std::uint32_t frame = 0; frame < kDetectorWarmupFrames; ++frame) {
        CHECK(!detector.process_frame(dc_only.data(), dc_only.size(), frame * 10));
    }
    CHECK(detector.features().rms < 1.0f);

    const auto sharp = impulse(5'000, 30'000);
    CHECK(detector.process_frame(sharp.data(), sharp.size(), 200));
    CHECK(detector.features().rms >= kClapAbsoluteFloor);
    CHECK(detector.features().peak / detector.features().rms >=
          kClapMinCrestFactor);
    CHECK(detector.features().diff_rms / detector.features().rms >=
          kClapMinDiffRatio);

    // An equally sharp frame inside the 200 ms refractory period is ignored;
    // the exact boundary is accepted.
    CHECK(!detector.process_frame(sharp.data(), sharp.size(), 399));
    CHECK(detector.process_frame(sharp.data(), sharp.size(), 400));

    detector.reset();
    std::array<std::int16_t, kFrameSamples> sustained{};
    for (std::size_t i = 0; i < sustained.size(); ++i) {
        sustained[i] = static_cast<std::int16_t>((i % 2 == 0) ? 2'000 : -2'000);
    }
    for (std::uint32_t frame = 0; frame < 400; ++frame) {
        CHECK(!detector.process_frame(sustained.data(), sustained.size(), frame * 10));
    }
    CHECK(detector.features().noise_rms > 1'500.0f);
    CHECK(detector.features().threshold > kClapAbsoluteFloor);

    // Once the adaptive floor has risen, a moderate isolated impulse remains
    // below the five-times-noise threshold even though its shape is transient.
    const auto moderate = impulse(0, 8'000);
    CHECK(!detector.process_frame(moderate.data(), moderate.size(), 4'100));

    detector.reset();
    CHECK(!detector.process_frame(nullptr, 0, 0));
    CHECK(!detector.process_frame(dc_only.data(), 1, 0));

    return claptest::finish();
}
