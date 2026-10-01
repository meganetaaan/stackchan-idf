// SPDX-License-Identifier: BSL-1.0
#include "dance/head_stroke_detector.hpp"

namespace stackchan::dance {

bool HeadStrokeDetector::quiet_for_release(bool touched, std::uint32_t now_ms) noexcept
{
    if (touched) {
        quiet_active_ = false;
        return false;
    }
    if (!quiet_active_) {
        quiet_active_ = true;
        quiet_start_ms_ = now_ms;
    }
    return now_ms - quiet_start_ms_ >= kQuietMs;
}

void HeadStrokeDetector::clear_gesture() noexcept
{
    hit_ = {};
    hit_offset_ms_ = {};
    gesture_active_ = false;
    quiet_active_ = false;
}

void HeadStrokeDetector::reset() noexcept
{
    phase_ = Phase::AwaitQuiet;
    clear_gesture();
}

bool HeadStrokeDetector::observe(const std::array<std::uint8_t, 3>& values,
                                 std::uint32_t now_ms) noexcept
{
    bool touched = false;
    for (const auto value : values) {
        if (value > 3) {
            reset();
            return false;
        }
        touched = touched || value != 0;
    }
    if (phase_ == Phase::AwaitQuiet) {
        if (quiet_for_release(touched, now_ms)) {
            phase_ = Phase::Tracking;
            clear_gesture();
        }
        return false;
    }
    if (gesture_active_ && now_ms - gesture_start_ms_ > kGestureTimeoutMs) {
        reset();
        return false;
    }
    if (phase_ == Phase::AwaitRelease) {
        if (!quiet_for_release(touched, now_ms)) return false;
        reset();
        return true;
    }
    if (gesture_active_ && now_ms - last_touch_ms_ > kGapMs) {
        clear_gesture();
    }
    if (!touched) return false;
    if (!gesture_active_) {
        gesture_active_ = true;
        gesture_start_ms_ = now_ms;
    }
    last_touch_ms_ = now_ms;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index] == 3 && !hit_[index]) {
            hit_[index] = true;
            hit_offset_ms_[index] = now_ms - gesture_start_ms_;
        }
    }
    if (hit_[0] && hit_[1] && hit_[2]) {
        const auto a = hit_offset_ms_[0], b = hit_offset_ms_[1], c = hit_offset_ms_[2];
        const bool forward = a <= b && b <= c && a < c;
        const bool reverse = a >= b && b >= c && a > c;
        if (forward || reverse) {
            phase_ = Phase::AwaitRelease;
            quiet_active_ = false;
        } else {
            reset();
        }
    }
    return false;
}

} // namespace stackchan::dance
