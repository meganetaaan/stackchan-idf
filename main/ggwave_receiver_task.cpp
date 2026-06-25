// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "ggwave_receiver_task.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <M5Unified.h>

#include "ggwave_decoder/decoder.hpp"

namespace stackchan::app {

namespace {

constexpr const char* kTag = "ggwave";
constexpr TickType_t kI2sSettle = pdMS_TO_TICKS(20);

void receiver_task_entry(void* arg)
{
    auto* state = static_cast<SharedState*>(arg);
    if (state == nullptr) {
        ESP_LOGE(kTag, "null state, aborting task");
        vTaskDelete(nullptr);
        return;
    }

    stackchan::ggwave::DecoderConfig config{};
    auto decoder_result = stackchan::ggwave::Decoder::create(config);
    if (!decoder_result) {
        ESP_LOGE(kTag, "decoder init failed: %s",
                 stackchan::ggwave::to_string(decoder_result.error()).data());
        vTaskDelete(nullptr);
        return;
    }
    auto decoder = std::move(*decoder_result);

    std::vector<std::int16_t> frame(decoder.frame_samples());
    bool mic_owned = false;
    ESP_LOGI(kTag, "receiver task started (rate=%u Hz, frame=%u samples)",
             static_cast<unsigned>(decoder.sample_rate_hz()),
             static_cast<unsigned>(decoder.frame_samples()));

    for (;;) {
        const bool conversation_busy =
            state->conversation_active.load(std::memory_order_relaxed) &&
            !state->conversation_idle.load(std::memory_order_relaxed);
        const bool audio_streaming =
            state->audio_stream_active.load(std::memory_order_relaxed);
        if (conversation_busy || audio_streaming || M5.Speaker.isPlaying()) {
            if (mic_owned) {
                M5.Mic.end();
                mic_owned = false;
                vTaskDelay(kI2sSettle);
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
            mic_owned = true;
        }

        if (!M5.Mic.record(frame.data(), frame.size(), decoder.sample_rate_hz(), /*stereo=*/false)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        for (int i = 0; i < 200 && M5.Mic.isRecording(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        auto decoded = decoder.feed(frame);
        if (!decoded) {
            if (decoded.error() != stackchan::ggwave::DecodeError::EmptyFrame) {
                ESP_LOGD(kTag, "decode skipped: %s",
                         stackchan::ggwave::to_string(decoded.error()).data());
            }
            continue;
        }
        for (const std::string& payload : *decoded) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
            ESP_LOGI(kTag, "[ggwave] decoded: %s",
                     stackchan::ggwave::payload_for_log(
                         std::span<const std::uint8_t>{bytes, payload.size()}).c_str());
        }
    }
}

} // namespace

void start_ggwave_receiver_task(SharedState& state)
{
    xTaskCreatePinnedToCore(receiver_task_entry, "ggwave-rx", 6144, &state,
                            tskIDLE_PRIORITY + 2, nullptr, 1);
}

} // namespace stackchan::app
