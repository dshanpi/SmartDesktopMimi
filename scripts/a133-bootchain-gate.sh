#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 09make.inc (零九智造)
#
# Product-owned entrypoint for the board-verified A133 boot-chain gate. The
# parser and pack outputs are SDK-owned, so the reviewed SDK implementation is
# authenticated before it is executed. This prevents an SDK update from
# silently weakening the flash gate.
set -euo pipefail

IMAGE=${1:-}
SDK_ROOT=${2:-${AITVBOX_A133_SDK:-/home/ubuntu/A133-Tina5.0-v0.9}}
REVIEWED_GATE_SHA256=71e81a98c74feb329509d661766f8627b19186a775fa4342a16b7ceb83588537
SDK_GATE="${SDK_ROOT}/scripts/a133-bootchain-gate.sh"

fatal() {
    echo "BOOTCHAIN GATE: $*" >&2
    exit 65
}

[[ -n "${IMAGE}" ]] || fatal "usage: $0 IMAGE [SDK_ROOT]"
[[ -f "${IMAGE}" ]] || fatal "image does not exist: ${IMAGE}"
IMAGE=$(realpath "${IMAGE}") || fatal "cannot resolve image path: ${IMAGE}"
[[ -d "${SDK_ROOT}" ]] || fatal "SDK root does not exist: ${SDK_ROOT}"
[[ -x "${SDK_GATE}" ]] || fatal "reviewed SDK gate is unavailable: ${SDK_GATE}"

actual_sha256=$(sha256sum "${SDK_GATE}" | awk '{print $1}')
[[ "${actual_sha256}" == "${REVIEWED_GATE_SHA256}" ]] || fatal \
    "SDK gate changed; expected ${REVIEWED_GATE_SHA256}, got ${actual_sha256}. Re-review before flashing."

echo "BOOTCHAIN GATE: reviewed SDK implementation ${actual_sha256}"
exec "${SDK_GATE}" "${IMAGE}"
