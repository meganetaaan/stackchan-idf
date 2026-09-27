// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "shared_state.hpp"

namespace stackchan::board {
class LedStrip;
}

namespace stackchan::app::article03 {

// Starts either the one-shot direct runner or the clap listener selected by
// Kconfig. The caller must ensure no other long-lived task owns the microphone.
bool start(SharedState& state, stackchan::board::LedStrip& strip);

} // namespace stackchan::app::article03
