// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "dance/dance.hpp"

namespace {

using stackchan::dance::DanceController;
using stackchan::dance::DanceFrame;
using stackchan::dance::DanceOutcome;
using stackchan::dance::DanceRequest;
using stackchan::dance::DanceResult;
using stackchan::dance::Runtime;

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        ++failures;
    }
}
class FakeRuntime final : public Runtime {
public:
    std::uint32_t now_ms() noexcept override { return now_ms_; }

    void sleep_ms(std::uint32_t duration_ms) noexcept override
    {
        now_ms_ += duration_ms;
        if (stop_on_first_sleep && controller != nullptr) {
            stop_on_first_sleep = false;
            controller->request_stop();
        }
    }

    bool apply(const DanceFrame& frame) noexcept override
    {
        frames.push_back(frame);
        ++apply_count;
        if (reenter_on_first_apply && controller != nullptr) {
            reenter_on_first_apply = false;
            nested_result = controller->run({90, 1'000});
        }
        return fail_on_apply == 0 || apply_count != fail_on_apply;
    }

    std::uint32_t now_ms_{0};
    std::vector<DanceFrame> frames;
    DanceController* controller{nullptr};
    std::optional<DanceResult> nested_result;
    int apply_count{0};
    int fail_on_apply{0};
    bool reenter_on_first_apply{false};
    bool stop_on_first_sleep{false};
};

void expect_safe_frame(const DanceFrame& frame)
{
    expect(std::fabs(frame.yaw_deg) < 0.001f, "safe frame centers yaw");
    expect(std::fabs(frame.pitch_deg) < 0.001f, "safe frame centers pitch");
    expect(frame.servo_speed_override == 0, "safe frame restores default servo speed");
    expect(!frame.happy, "safe frame restores neutral expression");
    expect(frame.led_side == stackchan::dance::LedSide::Off, "safe frame switches LED off");
    expect(frame.tone_frequency_hz == 0, "safe frame stops tone triggers");
}

void test_completed_sequence()
{
    expect(stackchan::dance::kToneVolume == 192,
           "requested adjustment raises the tone volume to one hundred ninety-two");
    expect(stackchan::dance::kLedBrightness == 128,
           "requested adjustment raises LED brightness to one hundred twenty-eight");
    expect(stackchan::dance::kLedColor == 0x00FF69B4u,
           "requested adjustment changes the LED color to pink");
    FakeRuntime runtime;
    DanceController controller{runtime};
    runtime.controller = &controller;

    const auto result = controller.run({90, 3'000});
    expect(result.outcome == DanceOutcome::Completed, "normal run completes");
    expect(result.actual_tempo_bpm == 90, "normal run reports actual tempo");
    expect(result.actual_duration_ms == 3'000, "normal run reports active duration");
    expect(!controller.busy(), "controller releases busy flag");
    expect(!runtime.frames.empty(), "normal run emits frames");

    bool saw_left = false;
    bool saw_right = false;
    bool saw_left_led = false;
    bool saw_right_led = false;
    std::vector<std::uint16_t> melody;
    bool saw_center_return = false;
    for (const auto& frame : runtime.frames) {
        expect(std::fabs(frame.pitch_deg) < 0.001f, "dance keeps pitch centered");
        expect(std::fabs(frame.yaw_deg) <= stackchan::dance::kYawAmplitudeDeg,
               "dance stays within yaw amplitude");
        saw_left = saw_left || frame.yaw_deg < 0.0f;
        saw_right = saw_right || frame.yaw_deg > 0.0f;
        saw_left_led = saw_left_led || frame.led_side == stackchan::dance::LedSide::Left;
        saw_right_led = saw_right_led || frame.led_side == stackchan::dance::LedSide::Right;
        if (frame.tone_frequency_hz != 0) melody.push_back(frame.tone_frequency_hz);
        if (frame.happy) {
            expect(frame.servo_speed_override > 0, "motion frame selects a bounded servo speed");
            expect(frame.servo_speed_override <= stackchan::dance::kMaxMotionServoSpeed,
                   "motion frame stays within the servo speed bound");
        }
        if (frame.led_side != stackchan::dance::LedSide::Off) {
            expect(frame.led_brightness == stackchan::dance::kLedBrightness,
                   "dance uses low LED brightness");
        } else if (frame.happy && std::fabs(frame.yaw_deg) < 0.001f) {
            saw_center_return = true;
        }
    }
    expect(saw_left && saw_right, "dance visits both yaw directions");
    expect(saw_left_led && saw_right_led, "dance alternates both LED sides");
    expect(melody.size() >= 16, "dance emits several melody notes per beat");
    constexpr std::uint16_t kChordProgression[4][3] = {
        {523, 659, 784}, {440, 523, 659}, {349, 440, 523}, {392, 494, 587}};
    for (std::size_t note = 0; note < 16 && note < melody.size(); ++note) {
        const auto& chord = kChordProgression[note / stackchan::dance::kMelodyNotesPerBeat];
        expect(melody[note] == chord[0] || melody[note] == chord[1] || melody[note] == chord[2],
               "melody note belongs to the active C-Am-F-G chord");
    }
    expect(melody.size() >= 18, "melody reaches the repeated C chord");
    if (melody.size() >= 18) {
        expect(melody[16] == melody[0] && melody[17] == melody[1],
               "repeated chord reuses its arpeggio pattern within one dance");
    }
    expect(saw_center_return, "dance adds a centered off-beat accent");
    expect_safe_frame(runtime.frames.back());
    expect(runtime.now_ms_ == 3'000 + stackchan::dance::kCenterSettleMs,
           "run waits for center settle after active duration");
}

void test_ease_out_profile()
{
    FakeRuntime runtime;
    DanceController controller{runtime};
    constexpr std::uint32_t kBeatPeriodMs = 60'000u / 90u;
    constexpr std::uint32_t kSideMotionMs = kBeatPeriodMs / 2u;
    constexpr std::uint32_t kSegmentFrameCount =
        (kSideMotionMs + stackchan::dance::kMotionFrameMs - 1u) /
        stackchan::dance::kMotionFrameMs;

    const auto result = controller.run({90, 1'000});
    expect(result.outcome == DanceOutcome::Completed, "ease-out run completes");
    expect(std::fabs(stackchan::dance::kYawAmplitudeDeg - 15.0f) < 0.001f,
           "requested adjustment increases yaw amplitude to fifteen degrees");
    expect(kSegmentFrameCount >= 16,
           "ninety BPM motion uses at least sixteen frames per half beat");
    expect(runtime.frames.size() >= kSegmentFrameCount * 2 + 1,
           "ease-out run emits a complete side and center motion");

    float previous_yaw = 0.0f;
    float previous_delta = stackchan::dance::kYawAmplitudeDeg + 1.0f;
    std::uint16_t previous_speed = stackchan::dance::kMaxMotionServoSpeed + 1;
    for (std::uint32_t i = 0; i < kSegmentFrameCount; ++i) {
        const auto& frame = runtime.frames[i];
        const float delta = std::fabs(frame.yaw_deg - previous_yaw);
        expect(frame.led_side == stackchan::dance::LedSide::Left,
               "first side motion keeps left LED on");
        expect(frame.happy, "side motion keeps happy expression");
        expect(frame.yaw_deg < previous_yaw, "first side motion progresses left");
        expect(delta < previous_delta, "side displacement decreases near its endpoint");
        expect(frame.servo_speed_override < previous_speed,
               "side servo speed decreases near its endpoint");
        previous_yaw = frame.yaw_deg;
        previous_delta = delta;
        previous_speed = frame.servo_speed_override;
    }
    std::size_t first_segment_tones = 0;
    for (std::uint32_t i = 0; i < kSegmentFrameCount; ++i) {
        if (runtime.frames[i].tone_frequency_hz != 0) ++first_segment_tones;
    }
    expect(first_segment_tones >= 2, "half-beat side motion contains multiple melody notes");
    expect(runtime.frames.front().servo_speed_override >= 700,
           "first side step uses a visibly faster servo speed");
    expect(std::fabs(previous_yaw + stackchan::dance::kYawAmplitudeDeg) < 0.001f,
           "side motion reaches the configured amplitude");

    previous_delta = stackchan::dance::kYawAmplitudeDeg + 1.0f;
    previous_speed = stackchan::dance::kMaxMotionServoSpeed + 1;
    for (std::uint32_t i = kSegmentFrameCount;
         i < kSegmentFrameCount * 2; ++i) {
        const auto& frame = runtime.frames[i];
        const float delta = std::fabs(frame.yaw_deg - previous_yaw);
        expect(frame.led_side == stackchan::dance::LedSide::Off,
               "center motion keeps LED off");
        expect(frame.happy, "center motion keeps happy expression");
        expect(frame.yaw_deg > previous_yaw, "first center motion progresses toward center");
        expect(delta < previous_delta, "center displacement decreases near its endpoint");
        expect(frame.servo_speed_override < previous_speed,
               "center servo speed decreases near its endpoint");
        previous_yaw = frame.yaw_deg;
        previous_delta = delta;
        previous_speed = frame.servo_speed_override;
    }
    expect(std::fabs(previous_yaw) < 0.001f, "center motion reaches zero degrees");
}

void test_request_clamps()
{
    FakeRuntime low_runtime;
    DanceController low_controller{low_runtime};
    const auto low = low_controller.run({1, 1});
    expect(low.actual_tempo_bpm == stackchan::dance::kMinTempoBpm, "tempo clamps low");
    expect(low.actual_duration_ms == stackchan::dance::kMinDurationMs, "duration clamps low");

    FakeRuntime high_runtime;
    DanceController high_controller{high_runtime};
    const auto high = high_controller.run({1'000, 99'999});
    expect(high.actual_tempo_bpm == stackchan::dance::kMaxTempoBpm, "tempo clamps high");
    expect(high.actual_duration_ms == stackchan::dance::kMaxDurationMs, "duration clamps high");
}

void test_high_tempo_uses_full_beat_moves()
{
    FakeRuntime runtime;
    DanceController controller{runtime};

    const auto result = controller.run({200, 1'200});
    expect(result.outcome == DanceOutcome::Completed, "high-tempo run completes");
    expect(result.actual_tempo_bpm == 200, "high-tempo run preserves two hundred BPM");

    constexpr std::uint32_t kBeatMs = 300;
    constexpr std::uint32_t kFramesPerMove =
        (kBeatMs + stackchan::dance::kMotionFrameMs - 1u) /
        stackchan::dance::kMotionFrameMs;
    expect(runtime.frames.size() >= kFramesPerMove * 4 + 1,
           "high-tempo run emits four full-beat moves");

    expect(runtime.frames[0].led_side == stackchan::dance::LedSide::Left,
           "high-tempo first beat moves left");
    expect(runtime.frames[kFramesPerMove].led_side == stackchan::dance::LedSide::Both,
           "high-tempo second beat returns to center with both LEDs");
    expect(runtime.frames[kFramesPerMove * 2].led_side == stackchan::dance::LedSide::Right,
           "high-tempo third beat moves right");
    expect(runtime.frames[kFramesPerMove * 3].led_side == stackchan::dance::LedSide::Both,
           "high-tempo fourth beat returns to center with both LEDs");
    std::size_t high_tempo_tones = 0;
    for (const auto& frame : runtime.frames) {
        if (frame.tone_frequency_hz != 0) ++high_tempo_tones;
    }
    expect(high_tempo_tones >= 16, "high-tempo motion keeps four melody notes per beat");
}

void test_busy_rejection()
{
    FakeRuntime runtime;
    DanceController controller{runtime};
    runtime.controller = &controller;
    runtime.reenter_on_first_apply = true;

    const auto outer = controller.run({90, 1'000});
    expect(outer.outcome == DanceOutcome::Completed, "outer run completes");
    expect(runtime.nested_result.has_value(), "nested run was attempted");
    expect(runtime.nested_result->outcome == DanceOutcome::Busy, "nested run returns busy");
    expect(runtime.nested_result->actual_tempo_bpm == 0, "busy reports no actual tempo");
    expect(runtime.nested_result->actual_duration_ms == 0, "busy reports no active duration");
}

void test_interrupted_cleanup()
{
    FakeRuntime runtime;
    DanceController controller{runtime};
    runtime.controller = &controller;
    runtime.stop_on_first_sleep = true;

    const auto result = controller.run({90, 3'000});
    expect(result.outcome == DanceOutcome::Interrupted, "stop request interrupts run");
    expect(result.actual_duration_ms > 0 && result.actual_duration_ms < 3'000,
           "interrupted run reports partial duration");
    expect_safe_frame(runtime.frames.back());
}

void test_failed_output_cleanup()
{
    FakeRuntime runtime;
    DanceController controller{runtime};
    runtime.controller = &controller;
    runtime.fail_on_apply = 2;

    const auto result = controller.run({90, 3'000});
    expect(result.outcome == DanceOutcome::Failed, "output failure returns failed");
    expect(runtime.frames.size() >= 3, "cleanup is attempted after output failure");
    expect_safe_frame(runtime.frames.back());
}

} // namespace

int main()
{
    test_completed_sequence();
    test_ease_out_profile();
    test_request_clamps();
    test_high_tempo_uses_full_beat_moves();
    test_busy_rejection();
    test_interrupted_cleanup();
    test_failed_output_cleanup();

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all dance controller checks passed\n");
    return 0;
}
