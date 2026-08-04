// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clap_dance/clap_dance.hpp"
#include "servo_limits.hpp"
#include "shared_state.hpp"

namespace stackchan::app {

// Starts the single owner task. Requests may subsequently come from any
// FreeRTOS task; today demo_loop is the producer, and a future LLM tool handler
// can use the same queue-backed entry point.
void start_clap_dance_task(SharedState& state, const ServoLimits& limits,
                           bool outputs_available);
bool request_clap_dance(const clap_dance::Request& request) noexcept;

} // namespace stackchan::app
