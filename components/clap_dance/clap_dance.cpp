// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include "clap_dance/clap_dance.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace stackchan::clap_dance {

namespace {

bool reached(std::uint32_t now_ms, std::uint32_t target_ms) noexcept
{
    return static_cast<std::int32_t>(now_ms - target_ms) >= 0;
}

template <std::size_t N>
std::uint32_t median(std::array<std::uint32_t, N> values) noexcept
{
    std::sort(values.begin(), values.end());
    if constexpr (N % 2 == 0) {
        return (values[N / 2 - 1] + values[N / 2]) / 2;
    }
    return values[N / 2];
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

TempoBand tempo_band_for_interval(std::uint32_t interval_ms) noexcept
{
    if (interval_ms >= 700) {
        return TempoBand::Slow;
    }
    if (interval_ms >= 520) {
        return TempoBand::Normal;
    }
    return TempoBand::Fast;
}

bool Controller::start(const Request& request, std::uint32_t now_ms) noexcept
{
    if (phase_ != Phase::Idle) {
        return false;
    }

    request_ = request;
    result_.reset();
    if (request.tempo_source == TempoSource::Claps) {
        reset_collection(now_ms);
        return true;
    }

    accepted_claps_ = 0;
    observed_claps_ = 0;
    begin_dance(request.preset_band, TempoConfidence::Preset, now_ms);
    return true;
}

void Controller::reset_collection(std::uint32_t now_ms) noexcept
{
    phase_ = Phase::WaitingClaps;
    output_ = {};
    output_.phase = phase_;
    output_.led_active = true;
    output_.led_color = kWaitingLedColor;
    output_.led_brightness = kWaitingLedBrightness;
    collection_started_ms_ = now_ms;
    last_accepted_clap_ms_ = 0;
    clap_ack_off_ms_ = 0;
    intervals_.fill(0);
    interval_count_ = 0;
    accepted_claps_ = 0;
    observed_claps_ = 0;
    stable_evaluations_ = 0;
    emitted_beats_ = 0;
    selected_band_ = TempoBand::Normal;
    confidence_ = TempoConfidence::Preset;
    clap_ack_active_ = false;
}

void Controller::acknowledge_clap(std::uint32_t now_ms) noexcept
{
    output_.led_active = true;
    output_.led_color = kClapAckLedColor;
    output_.led_brightness = kMaxLedBrightness;
    clap_ack_off_ms_ = now_ms + kClapAckLedMs;
    clap_ack_active_ = true;
}

void Controller::append_interval(std::uint32_t interval_ms) noexcept
{
    if (interval_count_ < intervals_.size()) {
        intervals_[interval_count_++] = interval_ms;
    }
}

std::uint32_t Controller::latest_interval_median() const noexcept
{
    std::array<std::uint32_t, kStabilityIntervals> latest{};
    const std::size_t begin = static_cast<std::size_t>(interval_count_) - latest.size();
    std::copy_n(intervals_.begin() + begin, latest.size(), latest.begin());
    return median(latest);
}

bool Controller::latest_intervals_stable(std::uint32_t median_ms) const noexcept
{
    std::array<std::uint32_t, kStabilityIntervals> deviations{};
    const std::size_t begin = static_cast<std::size_t>(interval_count_) - deviations.size();
    for (std::size_t i = 0; i < deviations.size(); ++i) {
        const std::uint32_t value = intervals_[begin + i];
        deviations[i] = value >= median_ms ? value - median_ms : median_ms - value;
    }
    const std::uint32_t mad_ms = median(deviations);
    const std::uint32_t allowed_ms =
        std::max<std::uint32_t>(40, median_ms * 8 / 100);
    return mad_ms <= allowed_ms;
}

bool Controller::clap(std::uint32_t now_ms) noexcept
{
    if (phase_ != Phase::WaitingClaps) {
        return false;
    }

    // Queue delivery may lag the sampling timestamp, and candidates from a
    // previous session may still be waiting when a new session starts. Only
    // accept events from this session's half-open collection window. The
    // signed-difference comparison remains valid across uint32_t wrap.
    if (!reached(now_ms, collection_started_ms_)) {
        return false;
    }
    const std::uint32_t collection_deadline_ms =
        collection_started_ms_ + kCollectionTimeoutMs;
    if (reached(now_ms, collection_deadline_ms)) {
        conclude_collection(collection_deadline_ms, Outcome::TimedOut);
        return false;
    }

    ++observed_claps_;
    acknowledge_clap(now_ms);

    if (accepted_claps_ == 0) {
        accepted_claps_ = 1;
        last_accepted_clap_ms_ = now_ms;
    } else {
        const std::uint32_t interval_ms = now_ms - last_accepted_clap_ms_;
        if (interval_ms >= kMinClapIntervalMs && interval_ms <= kMaxClapIntervalMs) {
            append_interval(interval_ms);
            ++accepted_claps_;
            last_accepted_clap_ms_ = now_ms;

            if (interval_count_ >= kStabilityIntervals) {
                const std::uint32_t median_ms = latest_interval_median();
                if (latest_intervals_stable(median_ms)) {
                    ++stable_evaluations_;
                } else {
                    stable_evaluations_ = 0;
                }
                if (accepted_claps_ >= kMinClaps && stable_evaluations_ >= 2) {
                    begin_dance(tempo_band_for_interval(median_ms),
                                TempoConfidence::Stable, now_ms);
                    return true;
                }
            }
        } else if (interval_ms > kMaxClapIntervalMs) {
            // A long pause begins a new rhythm sequence. Too-close candidates
            // are ignored without shifting the last accepted timestamp.
            intervals_.fill(0);
            interval_count_ = 0;
            accepted_claps_ = 1;
            stable_evaluations_ = 0;
            last_accepted_clap_ms_ = now_ms;
        }
    }

    if (observed_claps_ >= kMaxClaps) {
        conclude_collection(now_ms, Outcome::NotEnoughClaps);
    }
    return true;
}

void Controller::conclude_collection(std::uint32_t now_ms,
                                     Outcome insufficient) noexcept
{
    if (accepted_claps_ >= kMinClaps &&
        interval_count_ >= kStabilityIntervals) {
        const std::uint32_t median_ms = latest_interval_median();
        begin_dance(tempo_band_for_interval(median_ms),
                    TempoConfidence::FallbackMedian, now_ms);
        return;
    }
    finish(insufficient);
}

void Controller::begin_dance(TempoBand band, TempoConfidence confidence,
                             std::uint32_t now_ms) noexcept
{
    selected_band_ = band;
    confidence_ = confidence;
    period_ms_ = period_ms_for(band);
    emitted_beats_ = 0;
    clap_ack_active_ = false;
    phase_ = Phase::Dancing;
    output_ = {};
    output_.phase = phase_;
    output_.servo_active = true;
    output_.pitch_deg = 0.0f;
    output_.speed = kServoSpeed;
    output_.led_active = true;
    emit_beat(now_ms);
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
        .tempo_band = selected_band_,
        .confidence = confidence_,
        .bpm = bpm_for(selected_band_),
        .clap_count = accepted_claps_,
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
    if (phase_ == Phase::WaitingClaps) {
        if (clap_ack_active_ && reached(now_ms, clap_ack_off_ms_)) {
            clap_ack_active_ = false;
            output_.led_color = kWaitingLedColor;
            output_.led_brightness = kWaitingLedBrightness;
        }
        if (reached(now_ms, collection_started_ms_ + kCollectionTimeoutMs)) {
            conclude_collection(now_ms, Outcome::TimedOut);
        }
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
