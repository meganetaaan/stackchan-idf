// SPDX-License-Identifier: BSL-1.0
#pragma once

#include "dance/head_stroke_detector.hpp"
#include <optional>

namespace stackchan::dance {

// Input-side mode state, not choreography. The adapter serializes access,
// including sensor sampling, with one mutex. No hardware is owned here.
class DanceModeInput {
public:
    enum class State { Disabled, Standby, Entering, Listening, Leaving, Dancing };
    using Reading = std::array<std::uint8_t, 3>;

    State state() const noexcept { return state_; }
    std::uint32_t revision() const noexcept { return revision_; }
    bool reads_head() const noexcept
    {
        return state_ == State::Standby || state_ == State::Listening;
    }
    void enable() noexcept { reset_input(); state_ = State::Standby; }
    void disable() noexcept { reset_input(); state_ = State::Disabled; }

    void observe(std::optional<Reading> reading, std::uint32_t now_ms) noexcept;
    bool claps_allowed(std::uint32_t now_ms) const noexcept;
    bool start_dance(std::uint32_t now_ms) noexcept;
    void finish_dance() noexcept;
    // Called only after the adapter has reset clap count and cleaned up I/O.
    void settle_transition() noexcept;

private:
    void reset_input() noexcept
    {
        detector_.reset();
        quiet_ = false;
        ++revision_;
    }
    State state_{State::Disabled};
    HeadStrokeDetector detector_;
    bool quiet_{false};
    std::uint32_t revision_{0};
    std::uint32_t quiet_since_ms_{0};
    std::uint32_t last_sample_ms_{0};
};

} // namespace stackchan::dance
