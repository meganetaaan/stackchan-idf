// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "ggwave_decoder/decoder.hpp"

#include <ggwave/ggwave.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<std::int16_t> encode_payload(std::string_view payload, std::uint32_t sample_rate_hz)
{
    ggwave_setLogFile(nullptr);

    auto params = ggwave_getDefaultParameters();
    params.sampleRateInp = static_cast<float>(sample_rate_hz);
    params.sampleRateOut = static_cast<float>(sample_rate_hz);
    params.sampleRate = static_cast<float>(sample_rate_hz);
    params.sampleFormatInp = GGWAVE_SAMPLE_FORMAT_I16;
    params.sampleFormatOut = GGWAVE_SAMPLE_FORMAT_I16;
    params.operatingMode = GGWAVE_OPERATING_MODE_RX_AND_TX;

    const int instance = ggwave_init(params);
    assert(instance >= 0);

    const int bytes = ggwave_encode(instance, payload.data(), static_cast<int>(payload.size()),
                                    GGWAVE_PROTOCOL_AUDIBLE_FAST, 25, nullptr, 1);
    assert(bytes > 0);

    std::vector<std::int16_t> wave(static_cast<std::size_t>(bytes) / sizeof(std::int16_t));
    const int written = ggwave_encode(instance, payload.data(), static_cast<int>(payload.size()),
                                      GGWAVE_PROTOCOL_AUDIBLE_FAST, 25, wave.data(), 0);
    assert(written == bytes);
    ggwave_free(instance);
    return wave;
}

} // namespace

int main()
{
    stackchan::ggwave::DecoderConfig config{};
    auto decoder_result = stackchan::ggwave::Decoder::create(config);
    assert(decoder_result);
    auto decoder = std::move(*decoder_result);
    assert(decoder.uses_real_backend());
    assert(decoder.feed({}).error() == stackchan::ggwave::DecodeError::EmptyFrame);

    const auto wave = encode_payload("hello", config.sample_rate_hz);
    std::vector<std::string> decoded;
    for (std::size_t offset = 0; offset < wave.size(); offset += config.frame_samples) {
        const auto count = std::min(config.frame_samples, wave.size() - offset);
        auto result = decoder.feed(std::span<const std::int16_t>{wave.data() + offset, count});
        assert(result);
        decoded.insert(decoded.end(), result->begin(), result->end());
    }
    assert(decoded.size() == 1);
    assert(decoded.front() == "hello");

    const std::uint8_t noisy[] = {'h', 'i', '\n', 0x01, 'x'};
    assert(stackchan::ggwave::payload_for_log(noisy) == "hi ?x");

    std::cout << "ggwave_decoder host tests passed\n";
    return 0;
}
