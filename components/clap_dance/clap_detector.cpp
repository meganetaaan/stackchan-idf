// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include "clap_dance/clap_dance.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace stackchan::clap_dance {

void ClapDetector::reset() noexcept
{
    features_ = {};
    noise_rms_ = 100.0f;
    previous_sample_ = 0.0f;
    frames_seen_ = 0;
    last_clap_ms_ = 0;
    has_previous_sample_ = false;
    has_last_clap_ = false;
}

bool ClapDetector::process_frame(const std::int16_t* samples, std::size_t count,
                                 std::uint32_t now_ms) noexcept
{
    if (samples == nullptr || count < 2) {
        return false;
    }

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += static_cast<double>(samples[i]);
    }
    const float mean = static_cast<float>(sum / static_cast<double>(count));

    double sum_squares = 0.0;
    double diff_squares = 0.0;
    float peak = 0.0f;
    float previous = has_previous_sample_
                         ? previous_sample_
                         : static_cast<float>(samples[0]) - mean;
    for (std::size_t i = 0; i < count; ++i) {
        const float centered = static_cast<float>(samples[i]) - mean;
        const float magnitude = std::abs(centered);
        peak = std::max(peak, magnitude);
        sum_squares += static_cast<double>(centered) * centered;
        const float difference = centered - previous;
        diff_squares += static_cast<double>(difference) * difference;
        previous = centered;
    }
    previous_sample_ = previous;
    has_previous_sample_ = true;

    const float rms = static_cast<float>(
        std::sqrt(sum_squares / static_cast<double>(count)));
    const float diff_rms = static_cast<float>(
        std::sqrt(diff_squares / static_cast<double>(count)));
    const float threshold =
        std::max(kClapAbsoluteFloor, noise_rms_ * kClapNoiseMultiplier);
    const float crest = rms > 0.0f ? peak / rms : 0.0f;
    const float diff_ratio = rms > 0.0f ? diff_rms / rms : 0.0f;
    const bool warmed_up = frames_seen_ >= kDetectorWarmupFrames;
    const bool outside_refractory =
        !has_last_clap_ || now_ms - last_clap_ms_ >= kClapRefractoryMs;
    const bool transient =
        rms >= threshold && crest >= kClapMinCrestFactor &&
        diff_ratio >= kClapMinDiffRatio;
    const bool candidate = warmed_up && outside_refractory && transient;

    if (candidate) {
        last_clap_ms_ = now_ms;
        has_last_clap_ = true;
    } else if (!transient) {
        // 10 ms frames and alpha=0.005 give a time constant close to 2 s.
        // Transient frames are frozen out even during warm-up/refractory so a
        // clap cannot raise its own floor merely because it was not emitted.
        constexpr float kNoiseAlpha = 0.005f;
        noise_rms_ += (rms - noise_rms_) * kNoiseAlpha;
        if (noise_rms_ < 1.0f) {
            noise_rms_ = 1.0f;
        }
    }
    if (frames_seen_ < kDetectorWarmupFrames) {
        ++frames_seen_;
    }

    features_ = ClapFeatures{
        .rms = rms,
        .peak = peak,
        .diff_rms = diff_rms,
        .noise_rms = noise_rms_,
        .threshold = threshold,
    };
    return candidate;
}

} // namespace stackchan::clap_dance
