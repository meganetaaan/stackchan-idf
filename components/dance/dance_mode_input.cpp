// SPDX-License-Identifier: BSL-1.0
#include "dance/dance_mode_input.hpp"

namespace stackchan::dance {

void DanceModeInput::observe(std::optional<Reading> reading, std::uint32_t now_ms) noexcept
{
    if (!reads_head() || !reading) {
        reset_input();
        return;
    }
    // A stale sample cannot certify release or complete an old stroke.
    if (now_ms - last_sample_ms_ > HeadStrokeDetector::kQuietMs) reset_input();
    last_sample_ms_ = now_ms;
    const bool released = (*reading)[0] == 0 && (*reading)[1] == 0 && (*reading)[2] == 0;
    if (!released) {
        if (quiet_) ++revision_;
        quiet_ = false;
    } else if (!quiet_) {
        quiet_ = true;
        quiet_since_ms_ = now_ms;
    }
    if (detector_.observe(*reading, now_ms)) {
        state_ = state_ == State::Standby ? State::Entering : State::Leaving;
        reset_input();
    }
}

bool DanceModeInput::claps_allowed(std::uint32_t now_ms) const noexcept
{
    return state_ == State::Listening && quiet_ &&
           now_ms - quiet_since_ms_ >= HeadStrokeDetector::kQuietMs &&
           now_ms - last_sample_ms_ <= HeadStrokeDetector::kQuietMs;
}

bool DanceModeInput::start_dance(std::uint32_t now_ms) noexcept
{
    if (!claps_allowed(now_ms)) return false;
    state_ = State::Dancing;
    reset_input();
    return true;
}

void DanceModeInput::finish_dance() noexcept
{
    if (state_ != State::Dancing) return;
    state_ = State::Listening;
    reset_input();
}

void DanceModeInput::settle_transition() noexcept
{
    if (state_ == State::Entering) state_ = State::Listening;
    else if (state_ == State::Leaving) state_ = State::Standby;
    else return;
    reset_input();
}

} // namespace stackchan::dance
