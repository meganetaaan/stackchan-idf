// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "dance/clap_tempo_estimator.hpp"

#include <algorithm>

namespace stackchan::dance {

namespace {

constexpr float kAbsoluteTriggerRms = 1'200.0f;
constexpr float kTriggerNoiseRatio = 4.0f;
constexpr float kReleaseNoiseRatio = 1.8f;
constexpr float kNoiseAlpha = 0.02f;
constexpr std::uint32_t kRefractoryMs = 180;
constexpr std::uint32_t kMinIntervalMs = 220;
constexpr std::uint32_t kMaxIntervalMs = 1'500;
constexpr std::uint32_t kSequenceTimeoutMs = 4'500;

} // namespace

ClapObservation ClapTempoEstimator::observe(float rms, std::uint32_t now_ms) noexcept
{
    if (rms < 0.0f) rms = 0.0f;
    if (!noise_initialized_) {
        noise_floor_ = rms;
        noise_initialized_ = true;
        return {};
    }

    if (clap_count_ > 0 && now_ms - clap_times_[0] > kSequenceTimeoutMs) {
        reset_sequence();
    }

    const float trigger = std::max(kAbsoluteTriggerRms, noise_floor_ * kTriggerNoiseRatio);
    const float release = std::max(kAbsoluteTriggerRms * 0.5f, noise_floor_ * kReleaseNoiseRatio);

    if (!armed_) {
        if (rms <= release) {
            armed_ = true;
            update_noise_floor(rms);
        }
        return {};
    }

    if (rms < trigger) {
        update_noise_floor(rms);
        return {};
    }

    armed_ = false;
    if (clap_count_ > 0 && now_ms - last_clap_ms_ < kRefractoryMs) {
        return {};
    }

    if (clap_count_ > 0) {
        const std::uint32_t interval_ms = now_ms - last_clap_ms_;
        if (interval_ms < kMinIntervalMs || interval_ms > kMaxIntervalMs) {
            reset_sequence();
        }
    }

    clap_times_[clap_count_++] = now_ms;
    last_clap_ms_ = now_ms;
    ClapObservation observation{
        .clap_detected = true,
        .clap_count = static_cast<std::uint8_t>(clap_count_),
        .tempo_bpm = std::nullopt,
    };

    if (clap_count_ == kRequiredClaps) {
        std::array<std::uint32_t, kRequiredClaps - 1> intervals{};
        for (std::size_t i = 0; i < intervals.size(); ++i) {
            intervals[i] = clap_times_[i + 1] - clap_times_[i];
        }
        std::sort(intervals.begin(), intervals.end());
        const std::uint32_t median_ms = intervals[intervals.size() / 2];
        observation.tempo_bpm = static_cast<std::uint16_t>((60'000u + median_ms / 2u) / median_ms);
        reset_sequence();
    }

    return observation;
}

void ClapTempoEstimator::reset() noexcept
{
    reset_sequence();
    noise_floor_ = 0.0f;
    noise_initialized_ = false;
    armed_ = true;
}

void ClapTempoEstimator::reset_sequence() noexcept
{
    clap_times_.fill(0);
    clap_count_ = 0;
    last_clap_ms_ = 0;
}

void ClapTempoEstimator::update_noise_floor(float rms) noexcept
{
    noise_floor_ += (rms - noise_floor_) * kNoiseAlpha;
}

} // namespace stackchan::dance
