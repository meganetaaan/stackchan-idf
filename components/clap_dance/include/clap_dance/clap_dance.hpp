// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <array>
#include <cstddef>
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
inline constexpr std::uint32_t kWaitingLedColor = 0xFF6000;
inline constexpr std::uint8_t kWaitingLedBrightness = 4;
inline constexpr std::uint32_t kClapAckLedColor = 0x00FFFF;
inline constexpr std::uint32_t kClapAckLedMs = 80;

inline constexpr std::uint32_t kMinClapIntervalMs = 300;
inline constexpr std::uint32_t kMaxClapIntervalMs = 1500;
inline constexpr std::uint8_t kMinClaps = 8;
inline constexpr std::uint8_t kMaxClaps = 20;
inline constexpr std::uint32_t kCollectionTimeoutMs = 20'000;
inline constexpr std::size_t kStabilityIntervals = 6;

inline constexpr float kClapAbsoluteFloor = 800.0f;
inline constexpr float kClapNoiseMultiplier = 5.0f;
inline constexpr float kClapMinCrestFactor = 2.5f;
inline constexpr float kClapMinDiffRatio = 0.7f;
inline constexpr std::uint32_t kClapRefractoryMs = 200;
inline constexpr std::uint16_t kDetectorWarmupFrames = 20;

std::uint32_t period_ms_for(TempoBand band) noexcept;
std::uint16_t bpm_for(TempoBand band) noexcept;
TempoBand tempo_band_for_interval(std::uint32_t interval_ms) noexcept;

struct ClapFeatures {
    float rms = 0.0f;
    float peak = 0.0f;
    float diff_rms = 0.0f;
    float noise_rms = 100.0f;
    float threshold = kClapAbsoluteFloor;
};

// Pure 10 ms PCM-frame detector. The frame length may vary, but callers
// should supply one frame every 10 ms so the noise EWMA has a ~2 s time
// constant. DC offset is removed independently for every frame.
class ClapDetector {
public:
    bool process_frame(const std::int16_t* samples, std::size_t count,
                       std::uint32_t now_ms) noexcept;
    void reset() noexcept;

    const ClapFeatures& features() const noexcept { return features_; }

private:
    ClapFeatures features_{};
    float noise_rms_ = 100.0f;
    float previous_sample_ = 0.0f;
    std::uint16_t frames_seen_ = 0;
    std::uint32_t last_clap_ms_ = 0;
    bool has_previous_sample_ = false;
    bool has_last_clap_ = false;
};

// Pure, allocation-free state machine. Platform code supplies a monotonic
// millisecond clock and applies Output to the physical servo and LED drivers.
class Controller {
public:
    // Starts a preset dance immediately or a bounded clap-collection session.
    bool start(const Request& request, std::uint32_t now_ms) noexcept;
    bool clap(std::uint32_t now_ms) noexcept;
    bool cancel(std::uint32_t now_ms) noexcept;
    void tick(std::uint32_t now_ms) noexcept;

    Phase phase() const noexcept { return phase_; }
    bool busy() const noexcept { return phase_ != Phase::Idle; }
    const Output& output() const noexcept { return output_; }
    std::optional<Result> take_result() noexcept;

private:
    void begin_dance(TempoBand band, TempoConfidence confidence,
                     std::uint32_t now_ms) noexcept;
    void acknowledge_clap(std::uint32_t now_ms) noexcept;
    void reset_collection(std::uint32_t now_ms) noexcept;
    void append_interval(std::uint32_t interval_ms) noexcept;
    std::uint32_t latest_interval_median() const noexcept;
    bool latest_intervals_stable(std::uint32_t median_ms) const noexcept;
    void conclude_collection(std::uint32_t now_ms, Outcome insufficient) noexcept;
    void emit_beat(std::uint32_t now_ms) noexcept;
    void finish(Outcome outcome) noexcept;

    Phase phase_ = Phase::Idle;
    Request request_{};
    Output output_{};
    std::uint32_t period_ms_ = 600;
    std::uint32_t next_beat_ms_ = 0;
    std::uint32_t led_off_ms_ = 0;
    std::uint8_t emitted_beats_ = 0;
    TempoBand selected_band_ = TempoBand::Normal;
    TempoConfidence confidence_ = TempoConfidence::Preset;
    std::uint32_t collection_started_ms_ = 0;
    std::uint32_t last_accepted_clap_ms_ = 0;
    std::uint32_t clap_ack_off_ms_ = 0;
    std::array<std::uint32_t, kMaxClaps - 1> intervals_{};
    std::uint8_t interval_count_ = 0;
    std::uint8_t accepted_claps_ = 0;
    std::uint8_t observed_claps_ = 0;
    std::uint8_t stable_evaluations_ = 0;
    bool clap_ack_active_ = false;
    std::optional<Result> result_;
};

} // namespace stackchan::clap_dance
