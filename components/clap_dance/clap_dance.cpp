// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include "clap_dance/clap_dance.hpp"

#include <cstdint>

namespace stackchan::clap_dance {

namespace {

bool reached(std::uint32_t now_ms, std::uint32_t target_ms) noexcept
{
    return static_cast<std::int32_t>(now_ms - target_ms) >= 0;
}

} // namespace

std::uint32_t period_ms_for(TempoBand band) noexcept
{
    switch (band) {
    case TempoBand::Slow:   return 800;
    case TempoBand::Normal: return 600;
    case TempoBand::Fast:   return 462;
    }
    return 600;
}

std::uint16_t bpm_for(TempoBand band) noexcept
{
    switch (band) {
    case TempoBand::Slow:   return 75;
    case TempoBand::Normal: return 100;
    case TempoBand::Fast:   return 130;
    }
    return 100;
}

bool Controller::start(const Request& request, std::uint32_t now_ms) noexcept
{
    if (phase_ != Phase::Idle || request.tempo_source != TempoSource::Preset) {
        return false;
    }

    request_ = request;
    period_ms_ = period_ms_for(request.preset_band);
    emitted_beats_ = 0;
    result_.reset();
    phase_ = Phase::Dancing;
    output_ = {};
    output_.phase = phase_;
    output_.servo_active = true;
    output_.pitch_deg = 0.0f;
    output_.speed = kServoSpeed;
    output_.led_active = true;
    emit_beat(now_ms);
    return true;
}

bool Controller::cancel(std::uint32_t /*now_ms*/) noexcept
{
    if (phase_ == Phase::Idle || phase_ == Phase::Ending) {
        return false;
    }
    finish(Outcome::Cancelled);
    return true;
}

void Controller::emit_beat(std::uint32_t now_ms) noexcept
{
    // Beat 1 starts left, then alternates. Eight beats therefore finish right.
    output_.yaw_deg = (emitted_beats_ % 2 == 0) ? -kYawAmplitudeDeg
                                                : +kYawAmplitudeDeg;
    output_.led_color = kBeatLedColor;
    output_.led_brightness = kMaxLedBrightness;
    output_.beat = static_cast<std::uint8_t>(emitted_beats_ + 1);
    ++emitted_beats_;
    led_off_ms_ = now_ms + kBeatLedPulseMs;
    next_beat_ms_ = now_ms + period_ms_;
}

void Controller::finish(Outcome outcome) noexcept
{
    phase_ = Phase::Ending;
    output_ = {};
    output_.phase = phase_;
    result_ = Result{
        .outcome = outcome,
        .source = request_.source,
        .tempo_band = request_.preset_band,
        .confidence = TempoConfidence::Preset,
        .bpm = bpm_for(request_.preset_band),
        .clap_count = 0,
        .beats = emitted_beats_,
    };
}

void Controller::tick(std::uint32_t now_ms) noexcept
{
    if (phase_ == Phase::Ending) {
        phase_ = Phase::Idle;
        output_.phase = phase_;
        return;
    }
    if (phase_ != Phase::Dancing) {
        return;
    }

    if (output_.led_brightness != 0 && reached(now_ms, led_off_ms_)) {
        output_.led_brightness = 0;
    }

    // Catch up after a delayed scheduler tick without extending the dance.
    while (emitted_beats_ < kDanceBeats && reached(now_ms, next_beat_ms_)) {
        const std::uint32_t scheduled_ms = next_beat_ms_;
        emit_beat(scheduled_ms);
    }

    if (emitted_beats_ == kDanceBeats && reached(now_ms, next_beat_ms_)) {
        finish(Outcome::Completed);
    }
}

std::optional<Result> Controller::take_result() noexcept
{
    auto result = result_;
    result_.reset();
    return result;
}

} // namespace stackchan::clap_dance
