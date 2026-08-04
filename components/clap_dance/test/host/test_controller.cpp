// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include <cstdint>
#include <limits>

#include "clap_dance/clap_dance.hpp"
#include "test_support.hpp"

using namespace stackchan::clap_dance;

int main()
{
    CHECK(period_ms_for(TempoBand::Slow) == 800);
    CHECK(period_ms_for(TempoBand::Normal) == 600);
    CHECK(period_ms_for(TempoBand::Fast) == 462);
    CHECK(bpm_for(TempoBand::Slow) == 75);
    CHECK(bpm_for(TempoBand::Normal) == 100);
    CHECK(bpm_for(TempoBand::Fast) == 130);

    Controller dance;
    const Request fixed{
        .source = StartSource::HeadTouch,
        .tempo_source = TempoSource::Preset,
        .preset_band = TempoBand::Normal,
    };
    CHECK(dance.start(fixed, 1'000));
    CHECK(!dance.start(fixed, 1'000));
    CHECK(dance.phase() == Phase::Dancing);
    CHECK(dance.output().servo_active);
    CHECK(dance.output().yaw_deg == -12.0f);
    CHECK(dance.output().pitch_deg == 0.0f);
    CHECK(dance.output().speed == 500);
    CHECK(dance.output().led_active);
    CHECK(dance.output().led_brightness <= kMaxLedBrightness);
    CHECK(dance.output().beat == 1);

    dance.tick(1'119);
    CHECK(dance.output().led_brightness == kMaxLedBrightness);
    dance.tick(1'120);
    CHECK(dance.output().led_brightness == 0);

    for (std::uint8_t beat = 2; beat <= kDanceBeats; ++beat) {
        dance.tick(1'000 + static_cast<std::uint32_t>(beat - 1) * 600);
        CHECK(dance.output().beat == beat);
        CHECK(dance.output().yaw_deg == ((beat % 2 == 0) ? +12.0f : -12.0f));
        CHECK(dance.output().speed <= kServoSpeed);
        CHECK(dance.output().led_brightness <= kMaxLedBrightness);
    }
    CHECK(dance.phase() == Phase::Dancing);
    dance.tick(1'000 + 8 * 600);
    CHECK(dance.phase() == Phase::Ending);
    CHECK(!dance.output().servo_active);
    CHECK(!dance.output().led_active);
    const auto result = dance.take_result();
    CHECK(result.has_value());
    CHECK(result->outcome == Outcome::Completed);
    CHECK(result->beats == 8);
    CHECK(result->bpm == 100);
    CHECK(result->confidence == TempoConfidence::Preset);
    dance.tick(1'000 + 8 * 600 + 1);
    CHECK(dance.phase() == Phase::Idle);

    const Request clap_request{
        .source = StartSource::ToolCall,
        .tempo_source = TempoSource::Claps,
        .preset_band = TempoBand::Normal,
    };
    CHECK(!dance.start(clap_request, 10'000));

    CHECK(dance.start(fixed, 20'000));
    CHECK(dance.cancel(20'100));
    CHECK(dance.phase() == Phase::Ending);
    const auto cancelled = dance.take_result();
    CHECK(cancelled.has_value());
    CHECK(cancelled->outcome == Outcome::Cancelled);

    // uint32_t clock wrap must not delay the next beat.
    Controller wrapping;
    constexpr std::uint32_t near_wrap =
        std::numeric_limits<std::uint32_t>::max() - 100;
    CHECK(wrapping.start(fixed, near_wrap));
    wrapping.tick(499); // near_wrap + 600, modulo 2^32
    CHECK(wrapping.output().beat == 2);
    CHECK(wrapping.output().yaw_deg == +12.0f);

    return claptest::finish();
}
