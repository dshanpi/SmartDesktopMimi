#!/bin/bash
# ============================================================
# 100ask IoT - 设备签名工具 (工厂用)
#
# 用法:
#   ./sign_device.sh genkey              # 1. 生成密钥对 (仅一次!)
#   ./sign_device.sh sign <cpuid>        # 2. 签名单个设备
#   ./sign_device.sh batch <cpuid.txt>   # 3. 批量签名
#   ./sign_device.sh verify <cpuid> <sig> # 4. 验证 (调试)
#
# 原理:
#   ECDSA P-256 数字签名
#   - 私钥: 100ask 保管, 绝不外泄
#   - 公钥: 部署到后端, 用于验证设备真伪
#   - 签名: 142字符 hex, 烧入板子 /etc/100ask/device_sig
#
# 依赖: openssl >= 1.1.1
# ============================================================

set -e

KEY_DIR="${KEY_DIR:-./100ask_keys}"
PRIV_KEY="$KEY_DIR/100ask_ecdsa.pem"
PUB_KEY="$KEY_DIR/100ask_ecdsa.pub"
PUB_DER="$KEY_DIR/100ask_ecdsa_pub.der"

genkey() {
    mkdir -p "$KEY_DIR"
    if [ -f "$PRIV_KEY" ]; then
        echo "错误: $PRIV_KEY 已存在, 重新生成会使得所有已签名的设备失效!"
        echo "如果确认, 请先: rm -rf $KEY_DIR"
        exit 1
    fi

    echo "生成 ECDSA P-256 密钥对..."
    openssl ecparam -genkey -name prime256v1 -out "$PRIV_KEY"
    openssl ec -in "$PRIV_KEY" -pubout -out "$PUB_KEY"
    openssl ec -in "$PRIV_KEY" -pubout -outform DER -out "$PUB_DER"

    echo ""
    echo "============================================"
    echo "  密钥对已生成 (ECDSA P-256)"
    echo "  私钥: $PRIV_KEY  ← 妥善保管, 勿泄露!"
    echo "  公钥: $PUB_KEY   ← 部署到后端"
    echo "============================================"
    echo ""
    echo "公钥 (base64 DER, 部署到后端环境变量 IOT_PUBLIC_KEY):"
    base64 -w0 "$PUB_DER"
    echo ""
    echo ""
    echo "下一步:"
    echo "  1. 把上面这行公钥base64配置到后端"
    echo "  2. ./sign_device.sh sign <cpuid>  签名单个设备"
    echo "  3. 把签名烧入板子 /etc/100ask/device_sig"
}

sign() {
    local cpuid="$1"
    if [ -z "$cpuid" ]; then
        echo "用法: $0 sign <cpuid>"
        echo "示例: $0 sign 2848151601044f2400005c0000000000"
        exit 1
    fi
    [ -f "$PRIV_KEY" ] || { echo "错误: 未找到私钥, 请先 $0 genkey"; exit 1; }

    local msg="100ask:${cpuid}"
    echo -n "$msg" > /tmp/_sign_msg.bin
    openssl dgst -sha256 -sign "$PRIV_KEY" -out /tmp/_sign_sig.bin /tmp/_sign_msg.bin
    local sig_hex
    sig_hex=$(xxd -p /tmp/_sign_sig.bin | tr -d '\n')
    rm -f /tmp/_sign_msg.bin /tmp/_sign_sig.bin

    echo "cpuid:     $cpuid"
    echo "signature: $sig_hex"
    echo ""
    echo "# 烧录命令:"
    echo "echo '$sig_hex' > /etc/100ask/device_sig"
}

batch() {
    local file="$1"
    [ -n "$file" ] && [ -f "$file" ] || { echo "用法: $0 batch <cpuid_list.txt>"; exit 1; }
    [ -f "$PRIV_KEY" ] || { echo "错误: 未找到私钥, 请先 $0 genkey"; exit 1; }

    echo "# cpuid,signature_hex"
    while IFS= read -r cpuid; do
        [ -z "$cpuid" ] && continue
        local msg="100ask:${cpuid}"
        echo -n "$msg" > /tmp/_sign_msg.bin
        openssl dgst -sha256 -sign "$PRIV_KEY" -out /tmp/_sign_sig.bin /tmp/_sign_msg.bin
        local sig_hex
        sig_hex=$(xxd -p /tmp/_sign_sig.bin | tr -d '\n')
        echo "$cpuid,$sig_hex"
    done < "$file"
    rm -f /tmp/_sign_msg.bin /tmp/_sign_sig.bin
}

verify() {
    local cpuid="$1" sig_hex="$2"
    [ -n "$cpuid" ] && [ -n "$sig_hex" ] || { echo "用法: $0 verify <cpuid> <signature_hex>"; exit 1; }
    [ -f "$PUB_KEY" ] || { echo "错误: 未找到公钥, 请先 $0 genkey"; exit 1; }

    local msg="100ask:${cpuid}"
    echo -n "$msg" > /tmp/_verify_msg.bin
    echo -n "$sig_hex" | xxd -r -p > /tmp/_verify_sig.bin

    if openssl dgst -sha256 -verify "$PUB_KEY" -signature /tmp/_verify_sig.bin /tmp/_verify_msg.bin 2>/dev/null; then
        echo "✓ 签名有效 - 正版设备"
    else
        echo "✗ 签名无效 - 伪造设备!"
    fi
    rm -f /tmp/_verify_msg.bin /tmp/_verify_sig.bin
}

case "${1:-help}" in
    genkey) genkey ;;
    sign)   sign "$2" ;;
    batch)  batch "$2" ;;
    verify) verify "$2" "$3" ;;
    *)
        echo "100ask IoT - 设备签名工具 (ECDSA P-256)"
        echo ""
        echo "用法:"
        echo "  $0 genkey               生成 ECDSA P-256 密钥对"
        echo "  $0 sign <cpuid>         签名单个设备"
        echo "  $0 batch <cpuid.txt>    批量签名 (输出CSV)"
        echo "  $0 verify <cpuid> <sig> 验证签名 (调试)"
        echo ""
        echo "原理:"
        echo "  工厂用私钥签名芯片cpuid → 签名烧入板子"
        echo "  设备注册时发送 cpuid+签名 → 后端用公钥验证"
        echo "  每颗芯片cpuid唯一, 签名不可伪造, 无需数据库预登记"
        ;;
esac
