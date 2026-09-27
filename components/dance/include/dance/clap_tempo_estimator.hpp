// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace stackchan::dance {

struct ClapObservation {
    bool clap_detected{false};
    std::uint8_t clap_count{0};
    std::optional<std::uint16_t> tempo_bpm;
};

class ClapTempoEstimator {
public:
    static constexpr std::size_t kRequiredClaps = 4;

    ClapObservation observe(float rms, std::uint32_t now_ms) noexcept;
    void reset() noexcept;

private:
    void reset_sequence() noexcept;
    void update_noise_floor(float rms) noexcept;

    std::array<std::uint32_t, kRequiredClaps> clap_times_{};
    std::size_t clap_count_{0};
    std::uint32_t last_clap_ms_{0};
    float noise_floor_{0.0f};
    bool noise_initialized_{false};
    bool armed_{true};
};

} // namespace stackchan::dance
