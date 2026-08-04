// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <cstdint>
#include <optional>

namespace stackchan::clap_dance {

enum class StartSource : std::uint8_t {
    HeadTouch,
    ToolCall,
};

enum class TempoSource : std::uint8_t {
    Preset,
    Claps,
};

enum class TempoBand : std::uint8_t {
    Slow,
    Normal,
    Fast,
};

enum class TempoConfidence : std::uint8_t {
    Preset,
    Stable,
    FallbackMedian,
};

enum class Phase : std::uint8_t {
    Idle,
    WaitingClaps,
    Dancing,
    Ending,
};

enum class Outcome : std::uint8_t {
    Completed,
    Cancelled,
    TimedOut,
    NotEnoughClaps,
};

struct Request {
    StartSource source = StartSource::HeadTouch;
    TempoSource tempo_source = TempoSource::Preset;
    TempoBand preset_band = TempoBand::Normal;
};

struct Output {
    Phase phase = Phase::Idle;
    bool servo_active = false;
    float yaw_deg = 0.0f;
    float pitch_deg = 0.0f;
    std::uint16_t speed = 0;
    bool led_active = false;
    std::uint32_t led_color = 0;
    std::uint8_t led_brightness = 0;
    std::uint8_t beat = 0;
};

struct Result {
    Outcome outcome = Outcome::Completed;
    StartSource source = StartSource::HeadTouch;
    TempoBand tempo_band = TempoBand::Normal;
    TempoConfidence confidence = TempoConfidence::Preset;
    std::uint16_t bpm = 100;
    std::uint8_t clap_count = 0;
    std::uint8_t beats = 0;
};

inline constexpr std::uint8_t kDanceBeats = 8;
inline constexpr float kYawAmplitudeDeg = 12.0f;
inline constexpr std::uint16_t kServoSpeed = 500;
inline constexpr std::uint8_t kMaxLedBrightness = 16;
inline constexpr std::uint32_t kBeatLedColor = 0x0080FF;
inline constexpr std::uint32_t kBeatLedPulseMs = 120;

std::uint32_t period_ms_for(TempoBand band) noexcept;
std::uint16_t bpm_for(TempoBand band) noexcept;

// Pure, allocation-free state machine. Platform code supplies a monotonic
// millisecond clock and applies Output to the physical servo and LED drivers.
class Controller {
public:
    // Starts a preset-tempo dance. Clap-driven requests are deliberately
    // rejected until the clap collector has enough evidence to pick a band.
    bool start(const Request& request, std::uint32_t now_ms) noexcept;
    bool cancel(std::uint32_t now_ms) noexcept;
    void tick(std::uint32_t now_ms) noexcept;

    Phase phase() const noexcept { return phase_; }
    bool busy() const noexcept { return phase_ != Phase::Idle; }
    const Output& output() const noexcept { return output_; }
    std::optional<Result> take_result() noexcept;

private:
    void emit_beat(std::uint32_t now_ms) noexcept;
    void finish(Outcome outcome) noexcept;

    Phase phase_ = Phase::Idle;
    Request request_{};
    Output output_{};
    std::uint32_t period_ms_ = 600;
    std::uint32_t next_beat_ms_ = 0;
    std::uint32_t led_off_ms_ = 0;
    std::uint8_t emitted_beats_ = 0;
    std::optional<Result> result_;
};

} // namespace stackchan::clap_dance
