#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
# SPDX-License-Identifier: BSL-1.0

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${TMPDIR:-/tmp}/stackchan_ggwave_decoder_host_test"

g++ -std=c++20 -Wall -Wextra -Wno-dangling-reference \
    -I"${repo_root}/components/ggwave_decoder/include" \
    -I"${repo_root}/components/ggwave_decoder/vendor/ggwave/include" \
    -I"${repo_root}/components/ggwave_decoder/vendor/ggwave/src" \
    -I"${repo_root}/components/tl_expected/expected/include" \
    "${repo_root}/components/ggwave_decoder/src/decoder.cpp" \
    "${repo_root}/components/ggwave_decoder/vendor/ggwave/src/ggwave.cpp" \
    "${repo_root}/tools/ggwave_decoder/host_test.cpp" \
    -o "${out}"
"${out}"
