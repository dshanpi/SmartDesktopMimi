#!/usr/bin/env bash
# Package already-built images; private signing inputs stay outside both trees.
set -euo pipefail
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRODUCT_ROOT="$(cd "${SELF_DIR}/.." && pwd)"
source "${SELF_DIR}/lib.sh"
SDK="${1:-}"
SWU_OUTPUT="${2:-}"
[[ -n "$SDK" && -d "$SDK/.repo" ]] || die "usage: $0 <sdk-root>"
SDK="$(cd "$SDK" && pwd)"
: "${MIMI_SWU_SIGNING_KEY:?Set the external RSA private key path}"
: "${MIMI_SWU_PUBLIC_KEY:?Set the corresponding platform public key path}"
: "${MIMI_SYSTEM_VERSION:?Set the independent system release version}"
: "${SOURCE_DATE_EPOCH:?Set the fixed build epoch}"
SWU_OUTPUT="${SWU_OUTPUT:-$PRODUCT_ROOT/build/swu/$MIMI_SYSTEM_VERSION}"
ROOTFS="$SDK/out/a133/b6/openwrt/rootfs.img"
KERNEL="$SDK/out/a133/b6/openwrt/boot.img"
require_file "$ROOTFS" "Build the system rootfs before packaging"
require_file "$KERNEL" "Build the kernel image before packaging"
require_cmd debugfs "Install e2fsprogs on the build host"
# Refuse an image whose installed verification key differs from the signing key.
actual_key=$(mktemp)
trap 'rm -f -- "$actual_key"' EXIT
debugfs -R 'cat /etc/swupdate_public.pem' "$ROOTFS" > "$actual_key" 2>/dev/null
expected_der=$(openssl pkey -pubin -in "$MIMI_SWU_PUBLIC_KEY" -outform DER | sha256sum)
actual_der=$(openssl pkey -pubin -in "$actual_key" -outform DER | sha256sum)
[[ "$expected_der" == "$actual_der" ]] || die "rootfs SWUpdate key does not match selected signing key"
args=(--kernel "$KERNEL" --rootfs "$ROOTFS" --key "$MIMI_SWU_SIGNING_KEY"
      --public "$MIMI_SWU_PUBLIC_KEY" --version "$MIMI_SYSTEM_VERSION"
      --epoch "$SOURCE_DATE_EPOCH" --exclude-root "$SDK"
      --output "$SWU_OUTPUT")
if [[ -n "${MIMI_SWU_PASSWORD_FILE:-}" ]]; then
    args+=(--password-file "$MIMI_SWU_PASSWORD_FILE")
fi
python3 "$PRODUCT_ROOT/tools/update/swu_package.py" "${args[@]}"
echo "Local signed SWU prepared; real-device validation is still required."
