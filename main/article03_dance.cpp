// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "article03_dance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <M5Unified.h>

#include "avatar/expression.hpp"
#include "board/led_strip.hpp"
#include "board/si12t_touch.hpp"
#include "dance/clap_tempo_estimator.hpp"
#include "dance/dance.hpp"
#include "dance/dance_mode_input.hpp"

namespace stackchan::app::article03 {

namespace {

constexpr const char* kTag = "article03-dance";
constexpr std::uint32_t kSampleRate = 16'000;
constexpr std::size_t kChunkSamples = 256;
constexpr TickType_t kI2sSettleTicks = pdMS_TO_TICKS(20);
constexpr std::uint32_t kDirectStartDelayMs = 10'000;
constexpr std::uint32_t kPostDanceCooldownMs = 1'000;
constexpr std::uint32_t kClapFeedbackMs = 220;

using InputState = dance::DanceModeInput::State;
dance::DanceModeInput mode_input;
std::mutex input_mutex;

std::uint8_t scale_channel(std::uint8_t value, std::uint8_t brightness) noexcept
{
    return static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(value) * brightness) / 255u);
}

class SharedStateRuntime final : public dance::Runtime {
public:
    SharedStateRuntime(SharedState& state, stackchan::board::LedStrip& strip) noexcept
        : state_{state}, strip_{strip}, previous_speaker_volume_{M5.Speaker.getVolume()}
    {}

    std::uint32_t now_ms() noexcept override
    {
        return static_cast<std::uint32_t>(esp_timer_get_time() / 1'000);
    }

    void sleep_ms(std::uint32_t duration_ms) noexcept override
    {
        if (duration_ms == 0) return;
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
    }

    bool apply(const dance::DanceFrame& frame) noexcept override
    {
        // The existing servo audio guard normally freezes motion during
        // playback. Article 03 intentionally combines short, low-volume beat
        // accents with motion; keep that narrowly scoped to active frames.
        state_.servo.dance_active.store(frame.happy, std::memory_order_relaxed);
        state_.servo.speed_override.store(frame.servo_speed_override, std::memory_order_relaxed);
        state_.servo.target_pitch_deg.store(frame.pitch_deg, std::memory_order_relaxed);
        state_.servo.target_yaw_deg.store(frame.yaw_deg, std::memory_order_relaxed);
        state_.face.expression.store(
            static_cast<int>(frame.happy ? avatar::Expression::Happy : avatar::Expression::Neutral),
            std::memory_order_relaxed);

        // Article 03 owns the strip while its isolated demo mode is active.
        // Disable mouth modulation so the configured low brightness remains
        // deterministic even when NVS previously enabled lip-sync.
        state_.led.mouth_sync_enabled.store(false, std::memory_order_relaxed);
        state_.led.color.store(frame.led_color, std::memory_order_relaxed);
        state_.led.brightness.store(frame.led_brightness, std::memory_order_relaxed);
        const bool led_on = frame.led_side != dance::LedSide::Off;
        state_.led.mode.store(led_on ? 1 : 0, std::memory_order_relaxed);

        const bool led_changed = !last_led_frame_.has_value() ||
                                 last_led_frame_->side != frame.led_side ||
                                 last_led_frame_->color != frame.led_color ||
                                 last_led_frame_->brightness != frame.led_brightness;
        if (led_changed) {
            strip_.clear();
            if (led_on) {
                const std::uint8_t r = static_cast<std::uint8_t>((frame.led_color >> 16) & 0xFFu);
                const std::uint8_t g = static_cast<std::uint8_t>((frame.led_color >> 8) & 0xFFu);
                const std::uint8_t b = static_cast<std::uint8_t>(frame.led_color & 0xFFu);
                const std::size_t half = strip_.size() / 2u;
                const std::size_t begin = frame.led_side == dance::LedSide::Right ? half : 0u;
                const std::size_t end = frame.led_side == dance::LedSide::Left
                                            ? half
                                            : strip_.size();
                for (std::size_t index = begin; index < end; ++index) {
                    strip_.set(index,
                               scale_channel(r, frame.led_brightness),
                               scale_channel(g, frame.led_brightness),
                               scale_channel(b, frame.led_brightness));
                }
            }
            if (auto result = strip_.show(); !result) {
                ESP_LOGE(kTag, "back-panel LED update failed: %d",
                         static_cast<int>(result.error()));
                return false;
            }
            ESP_LOGI(kTag, "back-panel LED update side=%u brightness=%u pixels=%u",
                     static_cast<unsigned>(frame.led_side),
                     static_cast<unsigned>(frame.led_brightness),
                     static_cast<unsigned>(strip_.size()));
            last_led_frame_ = LedFrameState{
                .side = frame.led_side,
                .color = frame.led_color,
                .brightness = frame.led_brightness,
            };
        }

        if (frame.tone_frequency_hz != 0) {
            M5.Speaker.setVolume(dance::kToneVolume);
            if (!M5.Speaker.tone(frame.tone_frequency_hz, dance::kToneDurationMs)) {
                ESP_LOGE(kTag, "dance tone start failed: frequency_hz=%u",
                         static_cast<unsigned>(frame.tone_frequency_hz));
                return false;
            }
            const char* side = frame.led_side == dance::LedSide::Left
                                   ? "left"
                                   : frame.led_side == dance::LedSide::Right ? "right" : "center";
            ESP_LOGI(kTag, "dance accent side=%s frequency_hz=%u duration_ms=%u volume=%u",
                     side,
                     static_cast<unsigned>(frame.tone_frequency_hz),
                     static_cast<unsigned>(dance::kToneDurationMs),
                     static_cast<unsigned>(dance::kToneVolume));
        } else if (!frame.happy) {
            M5.Speaker.stop();
            M5.Speaker.setVolume(previous_speaker_volume_);
        }
        return true;
    }

    bool show_clap_feedback(std::uint8_t clap_count) noexcept
    {
        strip_.clear();
        const std::size_t lit_count = std::min(
            strip_.size(),
            (static_cast<std::size_t>(clap_count) * strip_.size() +
             dance::ClapTempoEstimator::kRequiredClaps - 1u) /
                dance::ClapTempoEstimator::kRequiredClaps);
        const std::uint8_t r = static_cast<std::uint8_t>((dance::kLedColor >> 16) & 0xFFu);
        const std::uint8_t g = static_cast<std::uint8_t>((dance::kLedColor >> 8) & 0xFFu);
        const std::uint8_t b = static_cast<std::uint8_t>(dance::kLedColor & 0xFFu);
        for (std::size_t index = 0; index < lit_count; ++index) {
            strip_.set(index,
                       scale_channel(r, dance::kLedBrightness),
                       scale_channel(g, dance::kLedBrightness),
                       scale_channel(b, dance::kLedBrightness));
        }
        if (auto result = strip_.show(); !result) {
            ESP_LOGE(kTag, "clap feedback LED update failed: %d",
                     static_cast<int>(result.error()));
            return false;
        }
        ESP_LOGI(kTag, "clap feedback LED update claps=%u brightness=%u lit=%u",
                 static_cast<unsigned>(clap_count),
                 static_cast<unsigned>(dance::kLedBrightness),
                 static_cast<unsigned>(lit_count));
        state_.led.mode.store(1, std::memory_order_relaxed);
        state_.led.color.store(dance::kLedColor, std::memory_order_relaxed);
        state_.led.brightness.store(dance::kLedBrightness, std::memory_order_relaxed);
        last_led_frame_.reset();
        return true;
    }

    bool clear_clap_feedback() noexcept
    {
        strip_.clear();
        if (auto result = strip_.show(); !result) {
            ESP_LOGE(kTag, "clap feedback LED clear failed: %d",
                     static_cast<int>(result.error()));
            return false;
        }
        state_.led.mode.store(0, std::memory_order_relaxed);
        last_led_frame_.reset();
        return true;
    }

private:
    struct LedFrameState {
        dance::LedSide side;
        std::uint32_t color;
        std::uint8_t brightness;
    };

    SharedState& state_;
    stackchan::board::LedStrip& strip_;
    std::uint8_t previous_speaker_volume_;
    std::optional<LedFrameState> last_led_frame_;
};

struct Context {
    Context(SharedState& state, stackchan::board::LedStrip& strip)
        : state{state}, runtime{state, strip}, controller{runtime}
    {}

    SharedState& state;
    SharedStateRuntime runtime;
    dance::DanceController controller;
};

float rms_amplitude(const std::array<std::int16_t, kChunkSamples>& samples) noexcept
{
    double sum = 0.0;
    for (const std::int16_t sample : samples) {
        const double value = static_cast<double>(sample);
        sum += value * value;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
}

dance::DanceResult run_and_log(Context& context, dance::DanceRequest request)
{
    const dance::DanceRequest normalized = dance::normalize_request(request);
    ESP_LOGI(kTag, "dance request requested_bpm=%u requested_duration_ms=%u",
             static_cast<unsigned>(request.tempo_bpm),
             static_cast<unsigned>(request.duration_ms));
    ESP_LOGI(kTag, "dance start actual_bpm=%u bounded_duration_ms=%u",
             static_cast<unsigned>(normalized.tempo_bpm),
             static_cast<unsigned>(normalized.duration_ms));

    const dance::DanceResult result = context.controller.run(request);
    ESP_LOGI(kTag,
             "dance finish outcome=%s actual_bpm=%u actual_duration_ms=%u safe_state=commanded",
             dance::to_string(result.outcome),
             static_cast<unsigned>(result.actual_tempo_bpm),
             static_cast<unsigned>(result.actual_duration_ms));
    return result;
}

void run_direct(Context& context)
{
    ESP_LOGI(kTag, "direct mode selected; one dance starts in %u ms",
             static_cast<unsigned>(kDirectStartDelayMs));
    vTaskDelay(pdMS_TO_TICKS(kDirectStartDelayMs));
    (void)run_and_log(context, {dance::kDirectTempoBpm, dance::kDirectDurationMs});
    ESP_LOGI(kTag, "direct mode idle; reboot to run again");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1'000));
    }
}

void run_clap_listener(Context& context)
{
    dance::ClapTempoEstimator estimator;
    std::array<std::int16_t, kChunkSamples> samples{};
    bool mic_owned = false;
    bool feedback_on = false;
    std::uint32_t feedback_off_at_ms = 0;

    // Mode changes are acknowledged only after microphone/feedback cleanup.
    // Pending transitions cannot be toggled again before the worker sees them.
    context.state.set_balloon_text("頭をなでてね", 3'000);
    ESP_LOGI(kTag, "standby; head stroke enters dance mode; mic=off");
    {
        std::lock_guard lock{input_mutex};
        mode_input.enable();
    }
    std::uint32_t input_revision = 0;

    for (;;) {
        const std::uint32_t loop_now_ms =
            static_cast<std::uint32_t>(esp_timer_get_time() / 1'000);
        InputState input_state;
        bool claps_allowed;
        bool input_changed;
        {
            std::lock_guard lock{input_mutex};
            input_state = mode_input.state();
            claps_allowed = mode_input.claps_allowed(context.runtime.now_ms());
            input_changed = input_revision != mode_input.revision();
            input_revision = mode_input.revision();
        }
        if (!claps_allowed || input_changed) {
            if (mic_owned) {
                M5.Mic.end();
                mic_owned = false;
                vTaskDelay(kI2sSettleTicks);
            }
            estimator.reset();
            const bool leaving = input_state == InputState::Leaving;
            if (feedback_on || leaving) {
                if (!context.runtime.clear_clap_feedback()) {
                    std::lock_guard lock{input_mutex};
                    mode_input.disable();
                    ESP_LOGE(kTag, "input disabled: failed to clear feedback");
                }
                feedback_on = false;
            }
            if (input_state == InputState::Entering || leaving) {
                InputState settled;
                {
                    std::lock_guard lock{input_mutex};
                    mode_input.settle_transition();
                    settled = mode_input.state();
                }
                if (settled == InputState::Listening) {
                    context.state.set_balloon_text("手拍子を4回", 3'000);
                    ESP_LOGI(kTag, "dance mode; waiting for claps; head stroke exits");
                } else if (settled == InputState::Standby) {
                    context.state.set_balloon_text("待機中", 3'000);
                    ESP_LOGI(kTag, "standby; clap count reset; mic=off");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (feedback_on &&
            static_cast<std::int32_t>(loop_now_ms - feedback_off_at_ms) >= 0) {
            (void)context.runtime.clear_clap_feedback();
            feedback_on = false;
        }

        if (M5.Speaker.isPlaying()) {
            if (mic_owned) {
                M5.Mic.end();
                mic_owned = false;
                estimator.reset();
                vTaskDelay(kI2sSettleTicks);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!mic_owned) {
            M5.Speaker.end();
            vTaskDelay(kI2sSettleTicks);
            if (!M5.Mic.begin()) {
                ESP_LOGW(kTag, "M5.Mic.begin failed; retrying in 5 s");
                for (int i = 0; i < 100; ++i) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                    std::lock_guard lock{input_mutex};
                    if (mode_input.state() != InputState::Listening ||
                        input_revision != mode_input.revision()) break;
                }
                continue;
            }
            mic_owned = true;
            estimator.reset();
        }

        if (!M5.Mic.record(samples.data(), samples.size(), kSampleRate, /*stereo=*/false)) {
            ESP_LOGW(kTag, "M5.Mic.record failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        for (int i = 0; i < 100 && M5.Mic.isRecording(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        // Discard audio captured across a head contact or mode transition.
        {
            std::lock_guard lock{input_mutex};
            if (!mode_input.claps_allowed(context.runtime.now_ms()) ||
                input_revision != mode_input.revision()) continue;
        }
        if (M5.Mic.isRecording()) {
            M5.Mic.end();
            mic_owned = false;
            estimator.reset();
            continue;
        }
        const float rms = rms_amplitude(samples);
        const std::uint32_t now_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1'000);
        const dance::ClapObservation observation = estimator.observe(rms, now_ms);
        if (observation.clap_detected) {
            ESP_LOGI(kTag, "clap detected index=%u rms=%.1f at_ms=%u",
                     static_cast<unsigned>(observation.clap_count),
                     static_cast<double>(rms),
                     static_cast<unsigned>(now_ms));
            if (context.runtime.show_clap_feedback(observation.clap_count)) {
                feedback_on = true;
                feedback_off_at_ms = now_ms + kClapFeedbackMs;
            }
        }
        if (!observation.tempo_bpm) {
            continue;
        }

        {
            // Atomically claim the dance against a pending exit or contact.
            // Sampling is under the same lock, so no in-flight read can
            // deliver an old gesture after the dance starts.
            std::lock_guard lock{input_mutex};
            if (input_revision != mode_input.revision() ||
                !mode_input.start_dance(context.runtime.now_ms())) continue;
        }

        ESP_LOGI(kTag, "clap tempo detected requested_bpm=%u claps=%u",
                 static_cast<unsigned>(*observation.tempo_bpm),
                 static_cast<unsigned>(dance::ClapTempoEstimator::kRequiredClaps));
        M5.Mic.end();
        mic_owned = false;
        feedback_on = false;
        vTaskDelay(kI2sSettleTicks);

        const auto result = run_and_log(context, {*observation.tempo_bpm, dance::kClapDurationMs});
        vTaskDelay(pdMS_TO_TICKS(kPostDanceCooldownMs));
        estimator.reset();
        {
            std::lock_guard lock{input_mutex};
            if (result.outcome == dance::DanceOutcome::Completed) mode_input.finish_dance();
            else mode_input.disable();
        }
        if (result.outcome != dance::DanceOutcome::Completed) {
            ESP_LOGE(kTag, "input disabled after unsuccessful dance");
            continue;
        }
        ESP_LOGI(kTag, "clap mode ready; waiting for %u claps",
                 static_cast<unsigned>(dance::ClapTempoEstimator::kRequiredClaps));
    }
}

void task_entry(void* arg)
{
    std::unique_ptr<Context> context{static_cast<Context*>(arg)};
    const dance::DanceFrame initial_safe_frame{
        .yaw_deg = 0.0f,
        .pitch_deg = 0.0f,
        .servo_speed_override = 0,
        .happy = false,
        .led_side = dance::LedSide::Off,
        .led_color = dance::kLedColor,
        .led_brightness = dance::kLedBrightness,
        .tone_frequency_hz = 0,
    };
    if (!context->runtime.apply(initial_safe_frame)) {
        ESP_LOGE(kTag, "failed to command initial safe state");
        // Never arm a new input while the initial output state is unknown.
        context.reset(); // vTaskDelete does not unwind C++ stack objects.
        vTaskDelete(nullptr);
        return;
    } else {
        ESP_LOGI(kTag, "initial safe state commanded");
    }
#if CONFIG_STACKCHAN_ARTICLE03_DANCE_DIRECT_AT_BOOT
    run_direct(*context);
#else
    run_clap_listener(*context);
#endif
}

} // namespace

bool start(SharedState& state, stackchan::board::LedStrip& strip,
           stackchan::board::Si12tTouch* head_touch)
{
#if !CONFIG_STACKCHAN_ARTICLE03_DANCE_DIRECT_AT_BOOT
    if (head_touch == nullptr) {
        ESP_LOGE(kTag, "head sensor unavailable; clap-dance task not started");
        return false;
    }
#else
    (void)head_touch; // development runner intentionally bypasses both inputs
#endif
    auto context = std::make_unique<Context>(state, strip);
    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_entry, "article03-dance", 6'144, context.get(), tskIDLE_PRIORITY + 3, nullptr, 1);
    if (rc != pdPASS) {
        ESP_LOGE(kTag, "failed to create Article 03 dance task");
        return false;
    }
    (void)context.release();
    return true;
}

void poll_head_touch(stackchan::board::Si12tTouch* touch, std::uint32_t now_ms)
{
    // I2C still runs on app_main. Serialize sampling with the worker's dance
    // claim; no sensor reads are accepted during dance or cooldown.
    static bool read_failed = false;
    std::lock_guard lock{input_mutex};
    if (!mode_input.reads_head()) return;
    if (touch == nullptr) {
        mode_input.observe(std::nullopt, now_ms);
        return;
    }
    const auto reading = touch->read_checked();
    if (!reading) {
        mode_input.observe(std::nullopt, now_ms);
        if (!read_failed) ESP_LOGW(kTag, "head sensor read failed; gesture discarded");
        read_failed = true;
        return;
    }
    read_failed = false;
    mode_input.observe(reading->intensities, now_ms);
    if (mode_input.state() == InputState::Entering || mode_input.state() == InputState::Leaving) {
        ESP_LOGI(kTag, "head stroke accepted after release; mode switch requested");
    }
}

} // namespace stackchan::app::article03
