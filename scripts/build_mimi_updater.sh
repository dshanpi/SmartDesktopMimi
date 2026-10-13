#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
go_bin=${GO:-/usr/local/go/bin/go}
output=${1:-"$root/build/mimi-update/mimi-update"}
mkdir -p "$(dirname "$output")"
cd "$root/services/mimi-update"
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 "$go_bin" build -buildvcs=false -trimpath -ldflags='-s -w' -o "$output" ./cmd/mimi-update
sha256sum "$output"
