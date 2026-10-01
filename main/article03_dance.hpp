// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "shared_state.hpp"

namespace stackchan::board {
class LedStrip;
class Si12tTouch;
}

namespace stackchan::app::article03 {

// Starts either the one-shot direct runner or the clap listener selected by
// Kconfig. The caller must ensure no other long-lived task owns the microphone.
bool start(SharedState& state, stackchan::board::LedStrip& strip,
           stackchan::board::Si12tTouch* head_touch);

// Call only from app_main, next to M5.update(), to keep sensor I2C on its
// existing owner task. No sensor reads or gestures are accepted while dancing.
void poll_head_touch(stackchan::board::Si12tTouch* head_touch,
                     std::uint32_t now_ms);

} // namespace stackchan::app::article03
