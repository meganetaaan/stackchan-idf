// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <atomic>
#include <cstdint>

namespace stackchan::dance {

struct DanceRequest {
    std::uint16_t tempo_bpm;
    std::uint32_t duration_ms;
};

enum class DanceOutcome : std::uint8_t {
    Completed,
    Busy,
    Interrupted,
    Failed,
};

struct DanceResult {
    DanceOutcome outcome;
    std::uint16_t actual_tempo_bpm;
    std::uint32_t actual_duration_ms;
};

enum class LedSide : std::uint8_t {
    Off,
    Left,
    Right,
    Both,
};

struct DanceFrame {
    float yaw_deg;
    float pitch_deg;
    std::uint16_t servo_speed_override;
    bool happy;
    LedSide led_side;
    std::uint32_t led_color;
    std::uint8_t led_brightness;
    std::uint16_t tone_frequency_hz;
};

inline constexpr std::uint16_t kMinTempoBpm = 60;
inline constexpr std::uint16_t kMaxTempoBpm = 200;
inline constexpr std::uint16_t kHalfBeatMotionMaxBpm = 120;
inline constexpr std::uint32_t kMinDurationMs = 1'000;
inline constexpr std::uint32_t kMaxDurationMs = 6'000;
inline constexpr std::uint16_t kDirectTempoBpm = 90;
inline constexpr std::uint32_t kDirectDurationMs = 3'000;
inline constexpr std::uint32_t kClapDurationMs = 6'000;
inline constexpr float kYawAmplitudeDeg = 15.0f;
inline constexpr std::uint32_t kMotionFrameMs = 20;
inline constexpr std::uint16_t kMaxMotionServoSpeed = 800;
inline constexpr std::uint32_t kLedColor = 0x00FF69B4u;
inline constexpr std::uint8_t kLedBrightness = 128;
inline constexpr std::uint16_t kLeftToneHz = 659;
inline constexpr std::uint16_t kRightToneHz = 784;
inline constexpr std::uint16_t kCenterToneHz = 523;
inline constexpr std::uint32_t kToneDurationMs = 50;
inline constexpr std::uint8_t kToneVolume = 192;
inline constexpr std::uint8_t kMelodyNotesPerBeat = 4;
inline constexpr std::uint32_t kCenterSettleMs = 350;

DanceRequest normalize_request(DanceRequest request) noexcept;
const char* to_string(DanceOutcome outcome) noexcept;

// Runtime is the only platform-specific boundary used by Controller. The
// firmware adapter writes SharedState atomics and sleeps with FreeRTOS; host
// tests use a deterministic fake clock and output recorder.
class Runtime {
public:
    virtual ~Runtime() = default;

    virtual std::uint32_t now_ms() noexcept = 0;
    virtual void sleep_ms(std::uint32_t duration_ms) noexcept = 0;
    virtual bool apply(const DanceFrame& frame) noexcept = 0;
};

// A synchronous, time-bounded dance operation. run() returns only after the
// active choreography ends and a best-effort safe-state command has been
// issued. Completed means the command sequence finished; physical motion must
// still be confirmed by a person at the device.
class DanceController {
public:
    explicit DanceController(Runtime& runtime) noexcept : runtime_{runtime} {}

    DanceResult run(DanceRequest request) noexcept;
    void request_stop() noexcept;
    bool busy() const noexcept;

private:
    bool apply_safe_state() noexcept;

    Runtime& runtime_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> stop_requested_{false};
};

} // namespace stackchan::dance
