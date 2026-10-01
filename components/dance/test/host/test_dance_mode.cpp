// SPDX-License-Identifier: BSL-1.0
#include "dance/dance_mode_input.hpp"
#include <cstdio>
#include <limits>

using stackchan::dance::DanceModeInput;
using State = DanceModeInput::State;
namespace {
int failures = 0;
void expect(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
struct Fixture {
    DanceModeInput input;
    std::uint32_t now = 0;
    void sample(DanceModeInput::Reading value)
    {
        now += 50;
        input.observe(value, now);
    }
    void quiet() { for (int i = 0; i < 4; ++i) sample({0, 0, 0}); }
    void stroke()
    {
        sample({3, 0, 0}); sample({0, 3, 0}); sample({0, 0, 3}); quiet();
    }
    void enter()
    {
        input.enable(); quiet(); stroke();
        expect(input.state() == State::Entering, "stroke requests entry");
        input.settle_transition(); quiet();
        expect(input.claps_allowed(now), "entry and quiet enable claps");
    }
};
void test_mode_cycle()
{
    Fixture f;
    expect(f.input.state() == State::Disabled, "disabled before initialization");
    f.input.enable(); f.quiet();
    expect(f.input.state() == State::Standby, "boot enters standby");
    expect(!f.input.start_dance(f.now), "standby ignores clap dance requests");
    f.stroke();
    expect(!f.input.start_dance(f.now), "pending entry cannot start dance");
    f.stroke();
    expect(f.input.state() == State::Entering, "entry remains latched until acknowledged");
    f.input.settle_transition(); f.quiet();
    for (int i = 0; i < 3; ++i) {
        expect(f.input.start_dance(f.now), "claps start repeated dances in mode");
        expect(!f.input.start_dance(f.now), "no duplicate dance");
        expect(!f.input.reads_head(), "no head sampling during dance/cooldown");
        f.quiet(); f.stroke();
        expect(f.input.state() == State::Dancing, "head stroke during dance ignored");
        f.input.finish_dance();
        expect(f.input.state() == State::Listening, "completion retains dance mode");
        expect(!f.input.claps_allowed(f.now), "completion needs fresh released samples");
        f.quiet();
    }
    const auto before = f.input.revision();
    f.stroke();
    expect(f.input.state() == State::Leaving, "second idle stroke requests exit");
    expect(f.input.revision() != before, "exit invalidates old clap sequence");
    expect(!f.input.start_dance(f.now), "exit beats a concurrent dance request");
    f.stroke();
    expect(f.input.state() == State::Leaving, "exit not lost to another stroke");
    f.input.settle_transition(); f.quiet();
    expect(f.input.state() == State::Standby, "exit returns to standby");
    expect(!f.input.claps_allowed(f.now), "standby does not listen");
    f.stroke(); f.input.settle_transition(); f.quiet();
    expect(f.input.start_dance(f.now), "mode can be entered again");
}
void test_contact_errors_and_stale_data()
{
    Fixture f; f.enter();
    auto revision = f.input.revision();
    f.sample({1, 0, 0});
    expect(!f.input.claps_allowed(f.now), "even weak contact suppresses clap input");
    expect(f.input.revision() != revision, "brief contact invalidates clap count");
    f.quiet();
    expect(f.input.claps_allowed(f.now), "quiet restores listening after incomplete touch");
    revision = f.input.revision();
    f.input.observe(std::nullopt, f.now + 1);
    expect(!f.input.start_dance(f.now + 1), "read failure blocks dance");
    expect(f.input.revision() != revision, "read failure invalidates count");
    f.quiet();
    expect(!f.input.start_dance(f.now + 151), "stale released sample blocks dance");
    f.now += 200; f.sample({0, 0, 0});
    expect(!f.input.claps_allowed(f.now), "sample gap requires fresh quiet interval");
    f.quiet();
    f.sample({4, 0, 0});
    expect(!f.input.start_dance(f.now), "invalid reading blocks dance");
    f.input.disable(); f.quiet(); f.stroke(); f.input.settle_transition();
    expect(f.input.state() == State::Disabled, "failure disables all inputs until reinitialized");
}
void test_boundary_and_wrap()
{
    Fixture f; f.now = std::numeric_limits<std::uint32_t>::max() - 250; f.enter();
    expect(f.input.start_dance(f.now), "clock wrap permits valid input");
    f.sample({3, 0, 0}); f.sample({0, 3, 0});
    f.input.finish_dance();
    f.sample({0, 0, 3}); f.quiet();
    expect(f.input.state() == State::Listening, "stroke across dance boundary cannot toggle");
    f.stroke();
    expect(f.input.state() == State::Leaving, "fresh stroke after dance can exit");
}
} // namespace
int main()
{
    test_mode_cycle(); test_contact_errors_and_stale_data(); test_boundary_and_wrap();
    if (failures) return 1;
    std::puts("all dance mode input checks passed");
    return 0;
}
