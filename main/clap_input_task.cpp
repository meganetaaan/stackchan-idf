// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include "clap_input_task.hpp"

#include <array>
#include <atomic>
#include <cstdint>

#include <M5Unified.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clap_dance/clap_dance.hpp"
#include "clap_dance_task.hpp"

namespace stackchan::app {

namespace {

constexpr const char* kTag = "clap-input";
constexpr std::uint32_t kSampleRate = 16'000;
constexpr std::size_t kFrameSamples = 160; // 10 ms
constexpr std::size_t kCaptureFrames = 10; // 100 ms keeps isRecording observable
constexpr std::size_t kCaptureSamples = kFrameSamples * kCaptureFrames;
constexpr std::uint32_t kFrameDurationMs = 10;
constexpr TickType_t kI2sSettle = pdMS_TO_TICKS(20);
constexpr std::uint32_t kCaptureTimeoutMs = 300;

std::atomic<bool> g_stop_requested{false};
std::atomic<TaskHandle_t> g_task{nullptr};
std::atomic<bool> g_ready{false};

enum class CaptureStatus : std::uint8_t {
    Completed,
    Interrupted,
    TimedOut,
};

CaptureStatus wait_for_capture() noexcept
{
    bool recording_started = false;
    for (std::uint32_t elapsed_ms = 0; elapsed_ms < kCaptureTimeoutMs;
         ++elapsed_ms) {
        const std::size_t recording_count = M5.Mic.isRecording();
        recording_started = recording_started || recording_count != 0;
        if (recording_started && recording_count == 0) {
            return CaptureStatus::Completed;
        }
        if (g_stop_requested.load(std::memory_order_acquire) ||
            M5.Speaker.isPlaying()) {
            return CaptureStatus::Interrupted;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return CaptureStatus::TimedOut;
}

void task_entry(void*)
{
    // M5Unified fills record() buffers from its own task. Keep the buffer
    // being recorded separate from the last completed buffer being analysed.
    // Static storage keeps the two 3.2 KiB capture buffers off this task's
    // small FreeRTOS stack.
    static std::array<std::array<std::int16_t, kCaptureSamples>, 2> frames{};
    std::array<std::uint32_t, 2> frame_timestamps{};
    std::size_t capture_index = 0;
    int completed_index = -1;
    clap_dance::ClapDetector detector;
    bool mic_owned = false;

    const auto cancel_session = [] {
        const auto now_ms =
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
        if (!cancel_clap_dance(now_ms)) {
            ESP_LOGW(kTag, "cancel command could not be queued");
        }
    };

    const auto release_mic = [&] {
        g_ready.store(false, std::memory_order_release);
        cancel_session();
        if (mic_owned) {
            M5.Mic.end();
            mic_owned = false;
            vTaskDelay(kI2sSettle);
        }
        detector.reset();
        completed_index = -1;
        capture_index = 0;
    };

    ESP_LOGI(kTag,
             "task started (rate=%u Hz frame=%u samples capture=%u ms)",
             static_cast<unsigned>(kSampleRate),
             static_cast<unsigned>(kFrameSamples),
             static_cast<unsigned>(kCaptureFrames * kFrameDurationMs));

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        // Settings-page test sounds can temporarily take the shared I2S bus.
        // Release the mic first and warm the detector again after reacquiring.
        if (M5.Speaker.isPlaying()) {
            if (mic_owned) {
                ESP_LOGW(kTag, "speaker took I2S; cancelling clap session");
                release_mic();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!mic_owned) {
            M5.Speaker.end();
            vTaskDelay(kI2sSettle);
            if (!M5.Mic.begin()) {
                ESP_LOGW(kTag, "M5.Mic.begin failed; retrying in 5 s");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            detector.reset();
            mic_owned = true;
            completed_index = -1;
            capture_index = 0;
            g_ready.store(true, std::memory_order_release);
            ESP_LOGI(kTag, "microphone ready");
        }

        auto& capture = frames[capture_index];
        if (!M5.Mic.record(capture.data(), capture.size(), kSampleRate,
                           /*stereo=*/false)) {
            ESP_LOGE(kTag, "record request failed; resetting microphone");
            release_mic();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // The previous buffer is complete and immutable while M5Unified fills
        // the other buffer. This avoids parsing partially written PCM.
        if (completed_index >= 0) {
            const auto batch_completed_at =
                frame_timestamps[completed_index];
            for (std::size_t frame_index = 0;
                 frame_index < kCaptureFrames; ++frame_index) {
                const auto detected_at = batch_completed_at -
                    static_cast<std::uint32_t>(
                        (kCaptureFrames - 1 - frame_index) * kFrameDurationMs);
                const auto* samples = frames[completed_index].data() +
                                      frame_index * kFrameSamples;
                if (!detector.process_frame(samples, kFrameSamples,
                                            detected_at)) {
                    continue;
                }
                const auto& f = detector.features();
                const bool queued = submit_clap(detected_at);
                ESP_LOGI(kTag,
                         "candidate t=%u rms=%.0f peak=%.0f diff=%.0f "
                         "noise=%.0f threshold=%.0f queue=%s",
                         static_cast<unsigned>(detected_at), f.rms, f.peak,
                         f.diff_rms, f.noise_rms, f.threshold,
                         queued ? "ok" : "full");
            }
        }

        const CaptureStatus capture_status = wait_for_capture();
        if (capture_status != CaptureStatus::Completed) {
            ESP_LOGE(kTag, "capture %s; resetting microphone",
                     capture_status == CaptureStatus::TimedOut
                         ? "timed out"
                         : "was interrupted");
            release_mic();
            continue;
        }

        frame_timestamps[capture_index] =
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
        completed_index = static_cast<int>(capture_index);
        capture_index ^= 1U;
    }

    release_mic();
    g_ready.store(false, std::memory_order_release);
    g_task.store(nullptr, std::memory_order_release);
    ESP_LOGI(kTag, "task stopped; microphone released");
    vTaskDelete(nullptr);
}

} // namespace

bool start_clap_input_task()
{
    if (g_task.load(std::memory_order_acquire) != nullptr) {
        return true;
    }
    g_ready.store(false, std::memory_order_release);
    g_stop_requested.store(false, std::memory_order_release);
    TaskHandle_t task = nullptr;
    const BaseType_t ok = xTaskCreatePinnedToCore(
        task_entry, "clap_input", 4096, nullptr, tskIDLE_PRIORITY + 2,
        &task, 1);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "task creation failed");
        return false;
    }
    g_task.store(task, std::memory_order_release);
    return true;
}

void stop_clap_input_task() noexcept
{
    g_ready.store(false, std::memory_order_release);
    g_stop_requested.store(true, std::memory_order_release);
    const auto now_ms =
        static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    (void)cancel_clap_dance(now_ms);
}

bool clap_input_ready() noexcept
{
    return g_ready.load(std::memory_order_acquire);
}

} // namespace stackchan::app
