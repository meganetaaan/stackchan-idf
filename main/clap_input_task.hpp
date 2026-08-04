// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#pragma once

namespace stackchan::app {

// Owns M5.Mic only in OperationMode::ClapDance and submits timestamped
// transient events to clap_dance_task. Stop is asynchronous and releases the
// microphone before the task exits.
bool start_clap_input_task();
void stop_clap_input_task() noexcept;
bool clap_input_ready() noexcept;

} // namespace stackchan::app
