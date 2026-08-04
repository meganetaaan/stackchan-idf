// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include <array>
#include <cstdint>
#include <limits>

#include "clap_dance/clap_dance.hpp"
#include "test_support.hpp"

using namespace stackchan::clap_dance;

namespace {

constexpr Request kFixed{
    .source = StartSource::HeadTouch,
    .tempo_source = TempoSource::Preset,
    .preset_band = TempoBand::Normal,
};

constexpr Request kClaps{
    .source = StartSource::ToolCall,
    .tempo_source = TempoSource::Claps,
    .preset_band = TempoBand::Normal,
};

void finish_dance(Controller& dance, std::uint32_t first_beat_ms,
                  std::uint32_t period_ms)
{
    dance.tick(first_beat_ms + period_ms * kDanceBeats);
    CHECK(dance.phase() == Phase::Ending);
    CHECK(!dance.output().servo_active);
    CHECK(!dance.output().led_active);
}

} // namespace

int main()
{
    CHECK(period_ms_for(TempoBand::Slow) == 800);
    CHECK(period_ms_for(TempoBand::Normal) == 600);
    CHECK(period_ms_for(TempoBand::Fast) == 462);
    CHECK(bpm_for(TempoBand::Slow) == 75);
    CHECK(bpm_for(TempoBand::Normal) == 100);
    CHECK(bpm_for(TempoBand::Fast) == 130);
    CHECK(tempo_band_for_interval(700) == TempoBand::Slow);
    CHECK(tempo_band_for_interval(699) == TempoBand::Normal);
    CHECK(tempo_band_for_interval(520) == TempoBand::Normal);
    CHECK(tempo_band_for_interval(519) == TempoBand::Fast);

    Controller fixed;
    CHECK(fixed.start(kFixed, 1'000));
    CHECK(!fixed.start(kFixed, 1'000));
    CHECK(fixed.phase() == Phase::Dancing);
    CHECK(fixed.output().servo_active);
    CHECK(fixed.output().yaw_deg == -12.0f);
    CHECK(fixed.output().pitch_deg == 0.0f);
    CHECK(fixed.output().speed == 500);
    CHECK(fixed.output().led_active);
    CHECK(fixed.output().led_brightness <= kMaxLedBrightness);
    CHECK(fixed.output().beat == 1);

    fixed.tick(1'119);
    CHECK(fixed.output().led_brightness == kMaxLedBrightness);
    fixed.tick(1'120);
    CHECK(fixed.output().led_brightness == 0);

    for (std::uint8_t beat = 2; beat <= kDanceBeats; ++beat) {
        fixed.tick(1'000 + static_cast<std::uint32_t>(beat - 1) * 600);
        CHECK(fixed.output().beat == beat);
        CHECK(fixed.output().yaw_deg == ((beat % 2 == 0) ? +12.0f : -12.0f));
        CHECK(fixed.output().speed <= kServoSpeed);
        CHECK(fixed.output().led_brightness <= kMaxLedBrightness);
    }
    finish_dance(fixed, 1'000, 600);
    const auto fixed_result = fixed.take_result();
    CHECK(fixed_result.has_value());
    CHECK(fixed_result->outcome == Outcome::Completed);
    CHECK(fixed_result->beats == 8);
    CHECK(fixed_result->bpm == 100);
    CHECK(fixed_result->confidence == TempoConfidence::Preset);
    fixed.tick(1'000 + 8 * 600 + 1);
    CHECK(fixed.phase() == Phase::Idle);

    // Seven 600 ms intervals make eight claps. The latest-six window is
    // stable on the seventh and eighth clap, so the eighth starts the dance.
    Controller stable;
    CHECK(stable.start(kClaps, 10'000));
    CHECK(stable.phase() == Phase::WaitingClaps);
    CHECK(!stable.output().servo_active);
    CHECK(stable.output().led_active);
    CHECK(stable.output().led_color == kWaitingLedColor);
    CHECK(stable.output().led_brightness == kWaitingLedBrightness);
    std::uint32_t clap_ms = 10'100;
    for (std::uint8_t clap = 1; clap <= kMinClaps; ++clap) {
        CHECK(stable.clap(clap_ms));
        CHECK(stable.output().led_brightness <= kMaxLedBrightness);
        if (clap == 1) {
            CHECK(stable.output().led_color == kClapAckLedColor);
            stable.tick(clap_ms + kClapAckLedMs);
            CHECK(stable.output().led_color == kWaitingLedColor);
        }
        if (clap < kMinClaps) {
            clap_ms += 600;
        }
    }
    CHECK(stable.phase() == Phase::Dancing);
    CHECK(stable.output().beat == 1);
    CHECK(stable.output().yaw_deg == -12.0f);
    finish_dance(stable, clap_ms, 600);
    const auto stable_result = stable.take_result();
    CHECK(stable_result.has_value());
    CHECK(stable_result->outcome == Outcome::Completed);
    CHECK(stable_result->tempo_band == TempoBand::Normal);
    CHECK(stable_result->confidence == TempoConfidence::Stable);
    CHECK(stable_result->clap_count == 8);
    CHECK(stable_result->beats == 8);

    // A detector event queued before the session began must not become clap
    // one. Eight fresh in-window events are still required.
    Controller stale_candidate;
    CHECK(stale_candidate.start(kClaps, 20'000));
    CHECK(!stale_candidate.clap(19'999));
    CHECK(stale_candidate.phase() == Phase::WaitingClaps);
    for (std::uint8_t clap = 0; clap < kMinClaps; ++clap) {
        CHECK(stale_candidate.clap(
            20'100 + static_cast<std::uint32_t>(clap) * 600));
    }
    CHECK(stale_candidate.phase() == Phase::Dancing);

    // An unstable sequence may use the median only when the 20 s collection
    // bound is reached and at least eight claps survived interval validation.
    Controller fallback;
    CHECK(fallback.start(kClaps, 30'000));
    constexpr std::array<std::uint32_t, 8> unstable_claps{
        30'100, 30'400, 31'900, 32'200, 33'700, 34'000, 35'500, 35'800,
    };
    for (const auto at : unstable_claps) {
        CHECK(fallback.clap(at));
    }
    CHECK(fallback.phase() == Phase::WaitingClaps);
    fallback.tick(50'000);
    CHECK(fallback.phase() == Phase::Dancing);
    CHECK(fallback.output().servo_active);
    const std::uint32_t fallback_period =
        period_ms_for(tempo_band_for_interval(900));
    finish_dance(fallback, 50'000, fallback_period);
    const auto fallback_result = fallback.take_result();
    CHECK(fallback_result.has_value());
    CHECK(fallback_result->confidence == TempoConfidence::FallbackMedian);
    CHECK(fallback_result->clap_count == 8);

    Controller timeout;
    CHECK(timeout.start(kClaps, 60'000));
    for (std::uint32_t at = 60'100; at < 64'000; at += 650) {
        CHECK(timeout.clap(at));
    }
    timeout.tick(80'000);
    CHECK(timeout.phase() == Phase::Ending);
    CHECK(!timeout.output().servo_active);
    CHECK(!timeout.output().led_active);
    const auto timed_out = timeout.take_result();
    CHECK(timed_out.has_value());
    CHECK(timed_out->outcome == Outcome::TimedOut);
    CHECK(timed_out->beats == 0);

    // The collection interval is half-open. A clap stamped exactly at the
    // 20 s deadline cannot rescue seven valid claps or start motion.
    Controller late_candidate;
    CHECK(late_candidate.start(kClaps, 82'000));
    for (std::uint8_t clap = 0; clap < kMinClaps - 1; ++clap) {
        CHECK(late_candidate.clap(
            82'100 + static_cast<std::uint32_t>(clap) * 600));
    }
    CHECK(!late_candidate.clap(82'000 + kCollectionTimeoutMs));
    CHECK(late_candidate.phase() == Phase::Ending);
    CHECK(!late_candidate.output().servo_active);
    const auto late_result = late_candidate.take_result();
    CHECK(late_result.has_value());
    CHECK(late_result->outcome == Outcome::TimedOut);
    CHECK(late_result->clap_count == kMinClaps - 1);

    // Twenty too-close observations cannot be promoted to a dance.
    Controller capped;
    CHECK(capped.start(kClaps, 90'000));
    for (std::uint8_t i = 0; i < kMaxClaps; ++i) {
        CHECK(capped.clap(90'100 + static_cast<std::uint32_t>(i) * 100));
    }
    CHECK(capped.phase() == Phase::Ending);
    const auto capped_result = capped.take_result();
    CHECK(capped_result.has_value());
    CHECK(capped_result->outcome == Outcome::NotEnoughClaps);
    CHECK(capped_result->clap_count < kMinClaps);

    Controller cancelled;
    CHECK(cancelled.start(kClaps, 120'000));
    CHECK(cancelled.clap(120'100));
    CHECK(cancelled.cancel(120'200));
    CHECK(cancelled.phase() == Phase::Ending);
    const auto cancelled_result = cancelled.take_result();
    CHECK(cancelled_result.has_value());
    CHECK(cancelled_result->outcome == Outcome::Cancelled);

    // Both waiting timeout and beat scheduling must survive uint32_t wrap.
    constexpr std::uint32_t near_wrap =
        std::numeric_limits<std::uint32_t>::max() - 100;
    Controller wrapping_fixed;
    CHECK(wrapping_fixed.start(kFixed, near_wrap));
    wrapping_fixed.tick(499); // near_wrap + 600, modulo 2^32
    CHECK(wrapping_fixed.output().beat == 2);
    CHECK(wrapping_fixed.output().yaw_deg == +12.0f);

    Controller wrapping_wait;
    CHECK(wrapping_wait.start(kClaps, near_wrap));
    wrapping_wait.tick(kCollectionTimeoutMs - 101);
    CHECK(wrapping_wait.phase() == Phase::Ending);
    const auto wrapping_timeout = wrapping_wait.take_result();
    CHECK(wrapping_timeout.has_value());
    CHECK(wrapping_timeout->outcome == Outcome::TimedOut);

    return claptest::finish();
}
