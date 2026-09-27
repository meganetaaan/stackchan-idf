// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include <cstdio>
#include <optional>

#include "dance/clap_tempo_estimator.hpp"
#include "dance/dance.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        ++failures;
    }
}
stackchan::dance::ClapObservation emit_clap(stackchan::dance::ClapTempoEstimator& estimator,
                                             std::uint32_t at_ms)
{
    (void)estimator.observe(100.0f, at_ms - 20);
    return estimator.observe(5'000.0f, at_ms);
}

void test_silence_and_noise_do_not_trigger()
{
    stackchan::dance::ClapTempoEstimator estimator;
    std::optional<std::uint16_t> tempo;
    for (std::uint32_t now = 0; now < 5'000; now += 16) {
        const float rms = 150.0f + static_cast<float>((now / 16) % 7) * 70.0f;
        const auto observation = estimator.observe(rms, now);
        expect(!observation.clap_detected, "background noise is not a clap");
        if (observation.tempo_bpm) tempo = observation.tempo_bpm;
    }
    expect(!tempo.has_value(), "background noise produces no tempo");
}

void test_four_claps_produce_tempo()
{
    stackchan::dance::ClapTempoEstimator estimator;
    (void)estimator.observe(100.0f, 0);
    expect(emit_clap(estimator, 1'000).clap_count == 1, "first clap is counted");
    expect(emit_clap(estimator, 1'667).clap_count == 2, "second clap is counted");
    expect(emit_clap(estimator, 2'334).clap_count == 3, "third clap is counted");
    const auto fourth = emit_clap(estimator, 3'001);
    expect(fourth.clap_count == 4, "fourth clap is counted");
    expect(fourth.tempo_bpm.has_value(), "four claps produce tempo");
    expect(*fourth.tempo_bpm == 90, "667 ms clap intervals produce 90 BPM");
}

void test_reflection_is_not_double_counted()
{
    stackchan::dance::ClapTempoEstimator estimator;
    (void)estimator.observe(100.0f, 0);
    expect(emit_clap(estimator, 1'000).clap_count == 1, "reflection test counts first clap");
    (void)estimator.observe(100.0f, 1'040);
    const auto reflection = estimator.observe(5'000.0f, 1'100);
    expect(!reflection.clap_detected, "reflection inside refractory period is ignored");
    expect(emit_clap(estimator, 1'667).clap_count == 2, "next real clap remains second");
    expect(emit_clap(estimator, 2'334).clap_count == 3, "next real clap remains third");
    const auto fourth = emit_clap(estimator, 3'001);
    expect(fourth.tempo_bpm.has_value() && *fourth.tempo_bpm == 90,
           "reflection does not change estimated tempo");
}

void test_timeout_resets_sequence()
{
    stackchan::dance::ClapTempoEstimator estimator;
    (void)estimator.observe(100.0f, 0);
    (void)emit_clap(estimator, 1'000);
    (void)emit_clap(estimator, 1'700);
    const auto after_timeout = emit_clap(estimator, 5'600);
    expect(after_timeout.clap_count == 1, "timeout starts a new clap sequence");
    expect(!after_timeout.tempo_bpm.has_value(), "timeout does not emit stale tempo");
}

void test_interval_reset_and_controller_clamp()
{
    stackchan::dance::ClapTempoEstimator estimator;
    (void)estimator.observe(100.0f, 0);
    (void)emit_clap(estimator, 1'000);
    (void)emit_clap(estimator, 1'700);
    const auto too_close = emit_clap(estimator, 1'900);
    expect(too_close.clap_count == 1, "out-of-range interval restarts sequence");
    expect(!too_close.tempo_bpm.has_value(), "out-of-range interval emits no tempo");

    estimator.reset();
    (void)estimator.observe(100.0f, 0);
    (void)emit_clap(estimator, 1'000);
    (void)emit_clap(estimator, 1'250);
    (void)emit_clap(estimator, 1'500);
    const auto fast = emit_clap(estimator, 1'750);
    expect(fast.tempo_bpm.has_value() && *fast.tempo_bpm == 240,
           "estimator reports a raw tempo for 250 ms claps");
    const auto normalized = stackchan::dance::normalize_request({*fast.tempo_bpm, 3'000});
    expect(normalized.tempo_bpm == stackchan::dance::kMaxTempoBpm,
           "dance core clamps fast detected tempo to two hundred BPM");
}

} // namespace

int main()
{
    test_silence_and_noise_do_not_trigger();
    test_four_claps_produce_tempo();
    test_reflection_is_not_double_counted();
    test_timeout_resets_sequence();
    test_interval_reset_and_controller_clamp();

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all clap tempo checks passed\n");
    return 0;
}
