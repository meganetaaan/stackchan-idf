// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "ggwave_decoder/decoder.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <ggwave/ggwave.h>

namespace stackchan::ggwave {

namespace {

constexpr std::size_t kMaxPayloadBytes = 256;

} // namespace

Decoder::Decoder(const DecoderConfig& config, int instance) : config_(config), instance_(instance) {}

Decoder::Decoder(Decoder&& other) noexcept : config_(other.config_), instance_(std::exchange(other.instance_, -1)) {}

Decoder& Decoder::operator=(Decoder&& other) noexcept
{
    if (this != &other) {
        reset();
        config_ = other.config_;
        instance_ = std::exchange(other.instance_, -1);
    }
    return *this;
}

Decoder::~Decoder()
{
    reset();
}

void Decoder::reset() noexcept
{
    if (instance_ >= 0) {
        ggwave_free(instance_);
        instance_ = -1;
    }
}

tl::expected<Decoder, DecodeError> Decoder::create(const DecoderConfig& config)
{
    if (config.sample_rate_hz == 0 || config.frame_samples == 0) {
        return tl::unexpected(DecodeError::InvalidFrame);
    }

    ggwave_setLogFile(nullptr);

    auto params = ggwave_getDefaultParameters();
    params.sampleRateInp = static_cast<float>(config.sample_rate_hz);
    params.sampleRate = static_cast<float>(config.sample_rate_hz);
    params.samplesPerFrame = static_cast<int>(config.frame_samples);
    params.sampleFormatInp = GGWAVE_SAMPLE_FORMAT_I16;
    params.operatingMode = GGWAVE_OPERATING_MODE_RX;

    const int instance = ggwave_init(params);
    if (instance < 0) {
        return tl::unexpected(DecodeError::BackendUnavailable);
    }
    return Decoder(config, instance);
}

bool Decoder::uses_real_backend() const
{
    return instance_ >= 0;
}

std::uint32_t Decoder::sample_rate_hz() const
{
    return config_.sample_rate_hz;
}

std::size_t Decoder::frame_samples() const
{
    return config_.frame_samples;
}

tl::expected<std::vector<std::string>, DecodeError> Decoder::feed(std::span<const std::int16_t> pcm)
{
    if (pcm.empty()) {
        return tl::unexpected(DecodeError::EmptyFrame);
    }
    if (instance_ < 0) {
        return tl::unexpected(DecodeError::BackendUnavailable);
    }

    std::array<std::uint8_t, kMaxPayloadBytes> payload{};
    const int decoded = ggwave_ndecode(instance_, pcm.data(), static_cast<int>(pcm.size_bytes()), payload.data(), payload.size());
    if (decoded == -2) {
        return tl::unexpected(DecodeError::InvalidFrame);
    }
    if (decoded < 0) {
        return std::vector<std::string>{};
    }
    if (decoded == 0) {
        return std::vector<std::string>{};
    }

    std::vector<std::string> result;
    result.emplace_back(reinterpret_cast<const char*>(payload.data()), static_cast<std::size_t>(decoded));
    return result;
}

std::string_view to_string(DecodeError error)
{
    switch (error) {
    case DecodeError::EmptyFrame:
        return "empty frame";
    case DecodeError::InvalidFrame:
        return "invalid frame";
    case DecodeError::BackendUnavailable:
        return "backend unavailable";
    }
    return "unknown";
}

std::string payload_for_log(std::span<const std::uint8_t> payload)
{
    std::string out;
    out.reserve(std::min(payload.size(), kMaxPayloadBytes));
    for (const std::uint8_t byte : payload) {
        if (byte == 0) break;
        if (byte == '\r' || byte == '\n' || byte == '\t') {
            out.push_back(' ');
        } else if (byte >= 0x20 && byte < 0x7f) {
            out.push_back(static_cast<char>(byte));
        } else if (byte >= 0x80) {
            out.push_back(static_cast<char>(byte));
        } else {
            out.push_back('?');
        }
        if (out.size() >= kMaxPayloadBytes) break;
    }
    return out;
}

} // namespace stackchan::ggwave
