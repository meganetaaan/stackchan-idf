// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "shared_state.hpp"

namespace stackchan::app {

// Background ggwave receiver. Milestone 1 owns the M5Unified mic only while
// conversation, idle JTTS, and speaker playback are inactive.
void start_ggwave_receiver_task(SharedState& state);

} // namespace stackchan::app
