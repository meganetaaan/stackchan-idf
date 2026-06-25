// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <tl/expected.hpp>

namespace stackchan::ggwave {

enum class DecodeError {
    EmptyFrame,
    InvalidFrame,
    BackendUnavailable,
};

struct DecoderConfig {
    std::uint32_t sample_rate_hz = 48'000;
    std::size_t frame_samples = 1024;
};

class Decoder {
public:
    static tl::expected<Decoder, DecodeError> create(const DecoderConfig& config);

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&& other) noexcept;
    Decoder& operator=(Decoder&& other) noexcept;
    ~Decoder();

    [[nodiscard]] bool uses_real_backend() const;
    [[nodiscard]] std::uint32_t sample_rate_hz() const;
    [[nodiscard]] std::size_t frame_samples() const;

    tl::expected<std::vector<std::string>, DecodeError> feed(std::span<const std::int16_t> pcm);

private:
    Decoder(const DecoderConfig& config, int instance);
    void reset() noexcept;

    DecoderConfig config_{};
    int instance_ = -1;
};

std::string_view to_string(DecodeError error);
std::string payload_for_log(std::span<const std::uint8_t> payload);

} // namespace stackchan::ggwave
