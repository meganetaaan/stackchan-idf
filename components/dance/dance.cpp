// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "dance/dance.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace stackchan::dance {

namespace {

std::uint32_t elapsed_since(std::uint32_t start_ms, std::uint32_t now_ms) noexcept
{
    return now_ms - start_ms;
}

float ease_out_cubic(float progress) noexcept
{
    const float remaining = 1.0f - progress;
    return 1.0f - remaining * remaining * remaining;
}

constexpr std::array<std::array<std::uint16_t, 3>, 4> kChordProgression{{
    {{523, 659, 784}}, // C:  C5 E5 G5
    {{440, 523, 659}}, // Am: A4 C5 E5
    {{349, 440, 523}}, // F:  F4 A4 C5
    {{392, 494, 587}}, // G:  G4 B4 D5
}};

constexpr std::array<std::array<std::uint8_t, kMelodyNotesPerBeat>, 16> kArpeggioPatterns{{
    {{0, 1, 2, 1}}, {{0, 2, 1, 2}}, {{1, 0, 2, 0}}, {{1, 2, 0, 2}},
    {{2, 0, 1, 0}}, {{2, 1, 0, 1}}, {{0, 1, 0, 2}}, {{0, 2, 0, 1}},
    {{1, 0, 1, 2}}, {{1, 2, 1, 0}}, {{2, 0, 2, 1}}, {{2, 1, 2, 0}},
    {{0, 0, 1, 2}}, {{1, 1, 2, 0}}, {{2, 2, 0, 1}}, {{0, 2, 1, 0}},
}};

std::uint32_t next_random(std::uint32_t& state) noexcept
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

std::uint16_t servo_speed_for_step(float delta_deg, std::uint32_t duration_ms) noexcept
{
    // SCS0009 Goal Speed is approximately 0.15 degrees per second per unit.
    // Match each eased command step to its scheduled duration, while keeping
    // the dance below a conservative bounded override.
    constexpr float kDegPerSecondPerSpeedUnit = 0.15f;
    if (duration_ms == 0 || delta_deg <= 0.0f) return 1;

    const float units =
        delta_deg * 1'000.0f /
        (static_cast<float>(duration_ms) * kDegPerSecondPerSpeedUnit);
    const auto rounded_units = static_cast<std::uint32_t>(units + 0.999f);
    return static_cast<std::uint16_t>(
        std::clamp<std::uint32_t>(rounded_units, 1u, kMaxMotionServoSpeed));
}

} // namespace

DanceRequest normalize_request(DanceRequest request) noexcept
{
    request.tempo_bpm = std::clamp(request.tempo_bpm, kMinTempoBpm, kMaxTempoBpm);
    request.duration_ms = std::clamp(request.duration_ms, kMinDurationMs, kMaxDurationMs);
    return request;
}

const char* to_string(DanceOutcome outcome) noexcept
{
    switch (outcome) {
    case DanceOutcome::Completed: return "completed";
    case DanceOutcome::Busy: return "busy";
    case DanceOutcome::Interrupted: return "interrupted";
    case DanceOutcome::Failed: return "failed";
    }
    return "failed";
}

DanceResult DanceController::run(DanceRequest request) noexcept
{
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return {.outcome = DanceOutcome::Busy, .actual_tempo_bpm = 0, .actual_duration_ms = 0};
    }

    struct BusyGuard {
        std::atomic<bool>& busy;
        ~BusyGuard() { busy.store(false, std::memory_order_release); }
    } guard{busy_};

    stop_requested_.store(false, std::memory_order_release);
    const DanceRequest actual = normalize_request(request);
    const std::uint32_t beat_period_ms =
        (60'000u + static_cast<std::uint32_t>(actual.tempo_bpm) / 2u) /
        static_cast<std::uint32_t>(actual.tempo_bpm);
    const std::uint16_t actual_tempo_bpm = static_cast<std::uint16_t>(
        (60'000u + beat_period_ms / 2u) / beat_period_ms);
    const bool high_tempo = actual.tempo_bpm > kHalfBeatMotionMaxBpm;
    const std::uint32_t side_motion_ms = high_tempo ? beat_period_ms : beat_period_ms / 2u;
    const std::uint32_t center_motion_ms =
        high_tempo ? beat_period_ms : beat_period_ms - side_motion_ms;
    const std::uint32_t start_ms = runtime_.now_ms();
    const std::uint32_t melody_step_ms = std::max<std::uint32_t>(
        1u, beat_period_ms / static_cast<std::uint32_t>(kMelodyNotesPerBeat));
    std::uint32_t next_melody_at_ms = 0;
    std::uint32_t melody_note_index = 0;
    std::uint32_t melody_random_state =
        start_ms ^ (static_cast<std::uint32_t>(actual.tempo_bpm) << 16u) ^ actual.duration_ms;
    if (melody_random_state == 0) melody_random_state = 0x6d2b79f5u;
    std::array<std::uint8_t, kChordProgression.size()> chord_pattern_indices{};
    for (auto& pattern_index : chord_pattern_indices) {
        pattern_index = static_cast<std::uint8_t>(
            next_random(melody_random_state) % kArpeggioPatterns.size());
    }

    DanceOutcome outcome = DanceOutcome::Completed;
    bool left = true;

    DanceFrame frame{
        .yaw_deg = 0.0f,
        .pitch_deg = 0.0f,
        .servo_speed_override = 0,
        .happy = true,
        .led_side = LedSide::Off,
        .led_color = kLedColor,
        .led_brightness = kLedBrightness,
        .tone_frequency_hz = 0,
    };

    auto move_ease_out = [&](float from_yaw, float to_yaw, LedSide led_side,
                             std::uint32_t segment_ms) noexcept -> bool {
        const std::uint32_t segment_start_ms = runtime_.now_ms();
        const std::uint32_t step_count =
            std::max<std::uint32_t>(1u, (segment_ms + kMotionFrameMs - 1u) / kMotionFrameMs);
        float previous_yaw = from_yaw;
        std::uint32_t previous_offset_ms = 0;

        for (std::uint32_t step = 1; step <= step_count; ++step) {
            if (elapsed_since(start_ms, runtime_.now_ms()) >= actual.duration_ms) {
                return false;
            }
            if (stop_requested_.load(std::memory_order_acquire)) {
                outcome = DanceOutcome::Interrupted;
                return false;
            }

            const std::uint32_t target_offset_ms = segment_ms * step / step_count;
            const std::uint32_t step_ms = target_offset_ms - previous_offset_ms;
            const float progress = static_cast<float>(step) /
                                   static_cast<float>(step_count);
            const float eased = ease_out_cubic(progress);

            frame.yaw_deg = from_yaw + (to_yaw - from_yaw) * eased;
            frame.servo_speed_override =
                servo_speed_for_step(std::fabs(frame.yaw_deg - previous_yaw), step_ms);
            frame.led_side = led_side;
            frame.tone_frequency_hz = 0;
            const std::uint32_t melody_elapsed_ms = elapsed_since(start_ms, runtime_.now_ms());
            if (melody_elapsed_ms >= next_melody_at_ms) {
                const std::uint32_t beat_index =
                    melody_note_index / static_cast<std::uint32_t>(kMelodyNotesPerBeat);
                const std::size_t chord_index = beat_index % kChordProgression.size();
                const auto& chord = kChordProgression[chord_index];
                const auto& pattern = kArpeggioPatterns[chord_pattern_indices[chord_index]];
                const std::size_t pattern_step = melody_note_index % kMelodyNotesPerBeat;
                frame.tone_frequency_hz = chord[pattern[pattern_step]];
                next_melody_at_ms += melody_step_ms;
                ++melody_note_index;
            }
            if (!runtime_.apply(frame)) {
                outcome = DanceOutcome::Failed;
                return false;
            }

            const std::uint32_t now_ms = runtime_.now_ms();
            const std::uint32_t active_elapsed_ms = elapsed_since(start_ms, now_ms);
            if (active_elapsed_ms >= actual.duration_ms) {
                return false;
            }

            const std::uint32_t segment_elapsed_ms = elapsed_since(segment_start_ms, now_ms);
            const std::uint32_t wait_ms = target_offset_ms > segment_elapsed_ms
                                              ? target_offset_ms - segment_elapsed_ms
                                              : 0;
            const std::uint32_t remaining_ms = actual.duration_ms - active_elapsed_ms;
            runtime_.sleep_ms(std::min(wait_ms, remaining_ms));

            previous_yaw = frame.yaw_deg;
            previous_offset_ms = target_offset_ms;
        }
        return true;
    };

    while (elapsed_since(start_ms, runtime_.now_ms()) < actual.duration_ms) {
        const float side_yaw = left ? -kYawAmplitudeDeg : kYawAmplitudeDeg;
        const LedSide led_side = left ? LedSide::Left : LedSide::Right;
        if (!move_ease_out(0.0f, side_yaw, led_side, side_motion_ms)) break;
        const LedSide center_led = high_tempo ? LedSide::Both : LedSide::Off;
        if (!move_ease_out(side_yaw, 0.0f, center_led, center_motion_ms)) break;
        left = !left;
    }

    const std::uint32_t active_duration_ms = elapsed_since(start_ms, runtime_.now_ms());
    if (!apply_safe_state()) {
        outcome = DanceOutcome::Failed;
    }
    runtime_.sleep_ms(kCenterSettleMs);

    return {
        .outcome = outcome,
        .actual_tempo_bpm = actual_tempo_bpm,
        .actual_duration_ms = active_duration_ms,
    };
}

void DanceController::request_stop() noexcept
{
    stop_requested_.store(true, std::memory_order_release);
}

bool DanceController::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

bool DanceController::apply_safe_state() noexcept
{
    return runtime_.apply({
        .yaw_deg = 0.0f,
        .pitch_deg = 0.0f,
        .servo_speed_override = 0,
        .happy = false,
        .led_side = LedSide::Off,
        .led_color = kLedColor,
        .led_brightness = kLedBrightness,
        .tone_frequency_hz = 0,
    });
}

} // namespace stackchan::dance
