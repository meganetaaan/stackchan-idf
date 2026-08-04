---
name: review-stackchan-motion
description: Review Stack-chan motion and light changes for physical safety, deterministic timing, input robustness, concurrency, and restoration of pre-existing state. Use when reviewing servo gestures, LED effects, touch or microphone triggers, clap-tempo logic, FreeRTOS task integration, or a diff that changes components/clap_dance, main/servo_task.cpp, main/led_task.cpp, or related SharedState fields.
---

# Review Stack-chan Motion

Review the requested diff without modifying files.
Treat the controller's observable behavior and the physical output limits as the primary contract.

## Establish the scope

1. Inspect `git status --short`, the requested diff, and adjacent callers and consumers.
2. Identify every path that can start, cancel, time out, or supersede the motion.
3. Trace each published output through `SharedState` to the servo and LED drivers.
4. Distinguish newly introduced defects from pre-existing behavior.

## Check the physical contract

- Reject any feature path that can command yaw outside `-12.0` through `+12.0` degrees or a servo speed above `500`.
- Require pitch to remain at zero for the clap dance and retain the servo driver's device-specific clamp as a second boundary.
- Reject expression or face-state writes from the clap-dance feature.
- Require LED brightness to stay at or below `16` and the beat effect to address the complete rear 12-pixel strip through the board abstraction.
- Require normal servo and LED state to remain untouched while an override is active.
- Require both override active flags to clear on completion, cancellation, timeout, task failure, and every rejected or aborted session.

## Check timing and state transitions

- Require exactly eight alternating beats and a bounded end state.
- Use wrap-safe comparisons for 32-bit millisecond clocks; flag raw future-time comparisons.
- Verify delayed scheduler ticks catch up without adding beats or extending the session indefinitely.
- Reject overlapping sessions unless replacement or cancellation semantics are explicit and tested.
- Verify queue-full and task-allocation failures are observable and leave outputs inactive.

## Check clap input changes

- Require microphone ownership to be exclusive to the dedicated mode and released when capture stops.
- Look for DC-offset handling, an adaptive noise floor, an absolute floor, transient-shape checks, and a refractory interval.
- Require accepted clap intervals to stay within 300 through 1500 milliseconds.
- Require at least eight claps before dancing, with no motion on insufficient evidence.
- Require the session to stop at 20 claps or 20 seconds.
- For a stable result, require the median and MAD of the latest six valid intervals, `MAD <= max(40 ms, median * 8%)`, on two consecutive evaluations.
- At the cap, allow a median fallback only when at least eight claps were accepted; label that confidence separately.
- Check boundary tests for slow at 700 milliseconds or more, normal at 520 through 699, and fast at 519 or less.
- Check that waiting and acknowledgement LED signals obey the same brightness limit and cannot leak into the dance phase.

## Verify proportionally

Run the smallest relevant host test suite first.
Run it again with AddressSanitizer and UndefinedBehaviorSanitizer when the host toolchain supports them.
For ESP-IDF integration changes, run the repository's documented build for the affected board without flashing hardware.
Use `git diff --check` before reporting.
Record commands that could not run and the exact reason; do not infer hardware behavior from a successful compile.

## Report findings

Lead with actionable findings ordered by severity: P0, P1, P2, then P3.
For each finding, name a precise file and line, describe the reproducible failure path, and state the smallest safe correction.
Keep summaries brief and place them after findings.
If no finding survives verification, say so explicitly and list residual hardware or acoustic risks separately.
