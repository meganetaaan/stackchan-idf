// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#include "clap_dance_task.hpp"

#include "clap_input_task.hpp"

#include <algorithm>
#include <cstdint>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace stackchan::app {

namespace {

constexpr const char* kTag = "clap-dance";
constexpr TickType_t kTaskPeriod = pdMS_TO_TICKS(20);
QueueHandle_t g_request_queue = nullptr;

enum class CommandType : std::uint8_t {
    Start,
    Clap,
    Cancel,
};

struct Command {
    CommandType type = CommandType::Start;
    clap_dance::Request request{};
    std::uint32_t timestamp_ms = 0;
};

bool has_safe_motion_range(const ServoLimits& limits) noexcept
{
    const float yaw_lo = std::max(static_cast<float>(limits.yaw_min_deg),
                                  -clap_dance::kYawAmplitudeDeg);
    const float yaw_hi = std::min(static_cast<float>(limits.yaw_max_deg),
                                  +clap_dance::kYawAmplitudeDeg);
    const bool pitch_zero_allowed =
        limits.pitch_min_deg <= 0 && limits.pitch_max_deg >= 0;
    return yaw_lo <= yaw_hi && pitch_zero_allowed;
}

void apply_output(SharedState& state, const clap_dance::Output& output)
{
    if (output.servo_active) {
        state.dance.yaw_deg.store(output.yaw_deg, std::memory_order_relaxed);
        state.dance.pitch_deg.store(output.pitch_deg, std::memory_order_relaxed);
        state.dance.speed.store(output.speed, std::memory_order_relaxed);
        state.dance.servo_active.store(true, std::memory_order_release);
    } else {
        state.dance.servo_active.store(false, std::memory_order_release);
    }

    if (output.led_active) {
        state.dance.led_color.store(output.led_color, std::memory_order_relaxed);
        state.dance.led_brightness.store(output.led_brightness,
                                          std::memory_order_relaxed);
        state.dance.led_active.store(true, std::memory_order_release);
    } else {
        state.dance.led_active.store(false, std::memory_order_release);
    }
}

void task_entry(void* arg)
{
    auto& state = *static_cast<SharedState*>(arg);
    clap_dance::Controller controller;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        const auto now_ms =
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
        Command command;
        while (xQueueReceive(g_request_queue, &command, 0) == pdTRUE) {
            if (command.type == CommandType::Start) {
                if (controller.start(command.request, command.timestamp_ms)) {
                    ESP_LOGI(kTag, "start source=%u tempo=%u band=%u",
                             static_cast<unsigned>(command.request.source),
                             static_cast<unsigned>(command.request.tempo_source),
                             static_cast<unsigned>(command.request.preset_band));
                } else {
                    ESP_LOGW(kTag, "request rejected phase=%u tempo_source=%u",
                             static_cast<unsigned>(controller.phase()),
                             static_cast<unsigned>(command.request.tempo_source));
                }
            } else if (command.type == CommandType::Clap) {
                (void)controller.clap(command.timestamp_ms);
            } else {
                (void)controller.cancel(command.timestamp_ms);
            }
        }

        controller.tick(now_ms);
        apply_output(state, controller.output());
        if (auto result = controller.take_result()) {
            ESP_LOGI(kTag,
                     "result outcome=%u source=%u band=%u confidence=%u "
                     "bpm=%u claps=%u beats=%u",
                     static_cast<unsigned>(result->outcome),
                     static_cast<unsigned>(result->source),
                     static_cast<unsigned>(result->tempo_band),
                     static_cast<unsigned>(result->confidence),
                     static_cast<unsigned>(result->bpm),
                     static_cast<unsigned>(result->clap_count),
                     static_cast<unsigned>(result->beats));
        }
        vTaskDelayUntil(&last_wake, kTaskPeriod);
    }
}

} // namespace

bool request_clap_dance(const clap_dance::Request& request) noexcept
{
    if (request.tempo_source == clap_dance::TempoSource::Claps &&
        !clap_input_ready()) {
        ESP_LOGW(kTag, "clap-tempo request rejected: input not ready");
        return false;
    }
    const Command command{
        .type = CommandType::Start,
        .request = request,
        .timestamp_ms =
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000),
    };
    return g_request_queue != nullptr &&
           xQueueSend(g_request_queue, &command, 0) == pdTRUE;
}

bool cancel_clap_dance(std::uint32_t timestamp_ms) noexcept
{
    const Command command{
        .type = CommandType::Cancel,
        .timestamp_ms = timestamp_ms,
    };
    return g_request_queue != nullptr &&
           xQueueSend(g_request_queue, &command, 0) == pdTRUE;
}

bool submit_clap(std::uint32_t timestamp_ms) noexcept
{
    const Command command{
        .type = CommandType::Clap,
        .timestamp_ms = timestamp_ms,
    };
    return g_request_queue != nullptr &&
           xQueueSend(g_request_queue, &command, 0) == pdTRUE;
}

void start_clap_dance_task(SharedState& state, const ServoLimits& limits,
                           bool outputs_available)
{
    if (g_request_queue != nullptr) {
        return;
    }
    if (!outputs_available || !has_safe_motion_range(limits)) {
        state.dance.servo_active.store(false, std::memory_order_release);
        state.dance.led_active.store(false, std::memory_order_release);
        ESP_LOGE(kTag,
                 "service disabled: outputs=%d yaw=[%d,%d] pitch=[%d,%d]",
                 outputs_available ? 1 : 0, limits.yaw_min_deg,
                 limits.yaw_max_deg, limits.pitch_min_deg,
                 limits.pitch_max_deg);
        return;
    }
    g_request_queue = xQueueCreate(12, sizeof(Command));
    if (g_request_queue == nullptr) {
        ESP_LOGE(kTag, "request queue allocation failed");
        return;
    }
    const BaseType_t ok = xTaskCreatePinnedToCore(
        task_entry, "clap_dance", 4096, &state, 3, nullptr, 0);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "task creation failed");
        vQueueDelete(g_request_queue);
        g_request_queue = nullptr;
    }
}

} // namespace stackchan::app
