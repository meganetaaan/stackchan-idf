# ggwave Milestone 1 Receiver

This branch adds the Stack-chan-side receiver task and decoder boundary for
ggwave audio reception on stackchan-idf / ESP-IDF 5.4.2.

Current backend status: `components/ggwave_decoder` vendors upstream
`ggerganov/ggwave` at commit `060aec7` (MIT) behind a small BSL-1.0 wrapper.
The host test proves a real ggwave encode/decode round trip through that wrapper.
CoreS3 hardware validation is still required for microphone gain/noise tuning.

## Enable

The receiver is compile-time gated and defaults off:

```sh
idf.py menuconfig
```

Enable:

`Stack-chan feature gates` -> `Enable ggwave receiver task`

Equivalent local default:

```sh
echo 'CONFIG_STACKCHAN_GGWAVE_RECEIVER_ENABLED=y' >> sdkconfig.defaults.local
```

For Milestone 1 bring-up, also keep conversation and idle JTTS disabled in the
device settings. The receiver only records while those always-on audio owners are
inactive, and yields while speaker/audio-stream playback is active, so it does
not fight the existing CoreS3 shared mic/speaker I2S path.

## Build, Flash, Monitor

```sh
tools/apply-m5-patches.sh
make set-target
make build
make flash PORT=/dev/ttyACM0
make monitor PORT=/dev/ttyACM0
```

On boot, expect:

```text
I (...) ggwave: receiver task started (rate=48000 Hz, frame=1024 samples)
```

Transmit `hello` from a PC or phone ggwave sender near the CoreS3 microphone.
The expected log boundary is:

```text
I (...) ggwave: [ggwave] decoded: hello
```

A convenient sender is the upstream ggwave web demo or CLI. Use an audible
protocol first, keep the speaker close to the CoreS3 mic, and start with a quiet
room before trying ultrasound/fast variants.

## Host Check

Without ESP-IDF hardware, run:

```sh
tools/ggwave_decoder/run_host_tests.sh
```

This validates:

- real upstream ggwave I16 encode/decode round trip for payload `hello`;
- empty-frame error handling;
- payload sanitisation used by the device log path.

## Limitations

- CoreS3 hardware validation is still required for microphone gain, frame timing,
  and false-positive/noise tuning.
- Speaker playback, conversation, BLE/Wi-Fi audio streaming, and mic lip-sync can
  interfere with microphone capture. Milestone 1 starts conservatively only when
  conversation and idle JTTS are disabled, and yields while the speaker or audio
  stream is active.
- Sender UI, Stack-chan-originated ggwave transmission, and command execution are
  intentionally left for later milestones.
