// SPDX-License-Identifier: BSL-1.0
#pragma once

#include <array>
#include <cstdint>

namespace stackchan::dance {

// Front/middle/back onset ordering follows the existing demo_loop gesture.
// A fresh quiet period arms the detector; a completed stroke is reported only
// after release. No servo or other output is driven while the hand is present.
class HeadStrokeDetector {
public:
    static constexpr std::uint32_t kQuietMs = 150;
    static constexpr std::uint32_t kGapMs = 600;
    static constexpr std::uint32_t kGestureTimeoutMs = 2'000;

    bool observe(const std::array<std::uint8_t, 3>& intensities,
                 std::uint32_t now_ms) noexcept;
    void reset() noexcept;

private:
    enum class Phase { AwaitQuiet, Tracking, AwaitRelease };
    bool quiet_for_release(bool touched, std::uint32_t now_ms) noexcept;
    void clear_gesture() noexcept;

    Phase phase_{Phase::AwaitQuiet};
    std::array<bool, 3> hit_{};
    std::array<std::uint32_t, 3> hit_offset_ms_{};
    bool gesture_active_{false};
    std::uint32_t gesture_start_ms_{0};
    std::uint32_t last_touch_ms_{0};
    bool quiet_active_{false};
    std::uint32_t quiet_start_ms_{0};
};

} // namespace stackchan::dance
