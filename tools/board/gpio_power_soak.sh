#!/usr/bin/env bash
# Cold-boot endurance test for the A133 desktop board.
#
# Every iteration cycles both configured Lynx power channels, proves that the
# Linux boot ID changed, and checks the application stack. Results are written
# after every iteration so an interrupted run can be resumed or audited.

set -uo pipefail

CYCLES="${CYCLES:-1000}"
DEVICE_IP="${DEVICE_IP:-192.168.1.62}"
MCP_URL="${MCP_URL:-http://127.0.0.1:18765/mcp}"
OFF_TIME_MS="${OFF_TIME_MS:-3000}"
BOOT_TIMEOUT_SECONDS="${BOOT_TIMEOUT_SECONDS:-150}"
SSH_KEY="${SSH_KEY:-/tmp/a133-deploy-key.XaTsYx/id_ed25519}"
LOG_DIR="${LOG_DIR:-/tmp/aitvbox-gpio-power-soak-$(date +%Y%m%d-%H%M%S)}"

mkdir -p "$LOG_DIR/serial" "$LOG_DIR/health"
RESULTS="$LOG_DIR/results.tsv"
SUMMARY="$LOG_DIR/summary.txt"
MCP_SESSION=""
SERIAL_HANDLE=""
MCP_REPLY=""
REQUEST_ID=100

for tool in curl python3 ssh awk sed grep date; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing required tool: $tool" >&2
        exit 2
    }
done

if [ ! -r "$SSH_KEY" ]; then
    echo "SSH key is not readable: $SSH_KEY" >&2
    exit 2
fi

SSH_OPTS=(
    -i "$SSH_KEY"
    -o BatchMode=yes
    -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/tmp/a133-known-hosts
    -o ConnectTimeout=20
    -o ServerAliveInterval=5
    -o ServerAliveCountMax=4
)

log() {
    printf '%s %s\n' "$(date '+%F %T')" "$*" | tee -a "$SUMMARY"
}

sse_json() {
    sed -n 's/^data: \({.*\)$/\1/p' | tail -n 1
}

mcp_initialize() {
    local headers response
    headers=$(mktemp "$LOG_DIR/mcp-headers.XXXXXX")
    response=$(curl -sS --max-time 20 -D "$headers" \
        -H 'Accept: application/json, text/event-stream' \
        -H 'Content-Type: application/json' \
        --data '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"aitvbox-gpio-power-soak","version":"1.0"}}}' \
        "$MCP_URL") || return 1
    MCP_SESSION=$(awk -F': ' 'tolower($1)=="mcp-session-id" {gsub("\\r", "", $2); print $2}' "$headers" | tail -n 1)
    rm -f "$headers"
    [ -n "$MCP_SESSION" ] || return 1
    printf '%s\n' "$response" | sse_json | python3 -c \
        'import json,sys; assert json.load(sys.stdin)["result"]["protocolVersion"]' || return 1
    curl -sS --max-time 10 \
        -H 'Accept: application/json, text/event-stream' \
        -H 'Content-Type: application/json' \
        -H "Mcp-Session-Id: $MCP_SESSION" \
        --data '{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}' \
        "$MCP_URL" >/dev/null || return 1
}

mcp_tool() {
    local name="$1" arguments="$2" payload response json attempt
    MCP_REPLY=""
    for attempt in 1 2; do
        if [ -z "$MCP_SESSION" ] && ! mcp_initialize; then
            continue
        fi
        REQUEST_ID=$((REQUEST_ID + 1))
        payload=$(python3 -c '
import json, sys
print(json.dumps({
    "jsonrpc": "2.0",
    "id": int(sys.argv[1]),
    "method": "tools/call",
    "params": {"name": sys.argv[2], "arguments": json.loads(sys.argv[3])},
}, separators=(",", ":")))
' "$REQUEST_ID" "$name" "$arguments")
        response=$(curl -sS --max-time 30 \
            -H 'Accept: application/json, text/event-stream' \
            -H 'Content-Type: application/json' \
            -H "Mcp-Session-Id: $MCP_SESSION" \
            --data "$payload" "$MCP_URL" 2>&1) || response=""
        json=$(printf '%s\n' "$response" | sse_json)
        if [ -n "$json" ] && printf '%s\n' "$json" | python3 -c '
import json, sys
data = json.load(sys.stdin)
assert "result" in data and not data["result"].get("isError", False)
' >/dev/null 2>&1; then
            MCP_REPLY="$json"
            return 0
        fi
        MCP_SESSION=""
        SERIAL_HANDLE=""
    done
    return 1
}

open_serial() {
    local json
    mcp_tool lynx_serial_open '{"port":"COM17","baud_rate":115200}' || return 1
    json="$MCP_REPLY"
    SERIAL_HANDLE=$(printf '%s\n' "$json" | python3 -c '
import json, sys
outer = json.load(sys.stdin)
inner = json.loads(outer["result"]["content"][0]["text"])
print(inner.get("handle", ""))
')
    [ -n "$SERIAL_HANDLE" ]
}

drain_serial() {
    local output="$1" json text
    if [ -z "$SERIAL_HANDLE" ] && ! open_serial; then
        return 1
    fi
    mcp_tool lynx_serial_read "{\"handle\":\"$SERIAL_HANDLE\",\"max_bytes\":65536,\"timeout_ms\":100}" || {
        SERIAL_HANDLE=""
        open_serial || return 1
        mcp_tool lynx_serial_read "{\"handle\":\"$SERIAL_HANDLE\",\"max_bytes\":65536,\"timeout_ms\":100}" || return 1
    }
    json="$MCP_REPLY"
    text=$(printf '%s\n' "$json" | python3 -c '
import json, sys
outer = json.load(sys.stdin)
inner = json.loads(outer["result"]["content"][0]["text"])
print(inner.get("text", ""), end="")
' 2>/dev/null) || return 1
    printf '%s' "$text" >>"$output"
}

remote_boot_id() {
    ssh "${SSH_OPTS[@]}" "root@$DEVICE_IP" 'cat /proc/sys/kernel/random/boot_id' 2>/dev/null
}

remote_health() {
    ssh "${SSH_OPTS[@]}" "root@$DEVICE_IP" '
        failures=""
        boot_id=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)
        uptime_s=$(cut -d" " -f1 /proc/uptime 2>/dev/null)
        for process in bluetoothd bluealsa lv_backend lvglsim aitvbox-appd aitvbox-controld aitvbox-ipkvmd aitvbox-kvm-video; do
            pidof "$process" >/dev/null 2>&1 || failures="$failures process:$process"
        done
        ipkvm=$(/usr/bin/aitvbox-ipkvmctl status 2>&1)
        echo "$ipkvm" | grep -q "web=running" || failures="$failures ipkvm:web"
        echo "$ipkvm" | grep -q "video=running" || failures="$failures ipkvm:video"
        echo "$ipkvm" | grep -q "control=ready" || failures="$failures ipkvm:control"
        echo "$ipkvm" | grep -q "stream=ready" || failures="$failures ipkvm:stream"
        grep -q ir_remote_libdriver /proc/bus/input/devices 2>/dev/null || failures="$failures infrared"
        bluetooth=$(bluetoothctl show 2>&1)
        echo "$bluetooth" | grep -q "Powered: yes" || failures="$failures bluetooth:powered"
        echo "$bluetooth" | grep -q "Discoverable: yes" || failures="$failures bluetooth:discoverable"
        echo "$bluetooth" | grep -q "Pairable: yes" || failures="$failures bluetooth:pairable"
        hid=$(/usr/bin/aitvbox-usb-hid-test 2>&1)
        echo "$hid" | grep -q "FAIL" && failures="$failures usb-hid"
        rm -f /tmp/aitvbox-soak-audio.wav
        arecord -q -D default -f S16_LE -r 16000 -c 1 -d 1 /tmp/aitvbox-soak-audio.wav >/dev/null 2>&1 || failures="$failures audio:capture"
        audio_bytes=$(wc -c </tmp/aitvbox-soak-audio.wav 2>/dev/null || echo 0)
        [ "$audio_bytes" -ge 32000 ] 2>/dev/null || failures="$failures audio:size:$audio_bytes"
        snapshot=$(/usr/bin/hdmi_preview --snapshot 2>&1)
        echo "$snapshot" | grep -q "OK" || failures="$failures hdmi:snapshot"
        snapshot_bytes=$(wc -c </var/run/aitvbox/screen.jpg 2>/dev/null || echo 0)
        [ "$snapshot_bytes" -ge 10000 ] 2>/dev/null || failures="$failures hdmi:size:$snapshot_bytes"
        if dmesg | grep -E "Kernel panic|Oops:|EXT[234]-fs error|Buffer I/O error|mmcblk.*I/O error|Out of memory: Kill process" >/tmp/aitvbox-soak-kernel-errors.txt; then
            failures="$failures kernel-error"
        fi
        printf "boot_id=%s\n" "$boot_id"
        printf "uptime_s=%s\n" "$uptime_s"
        printf "audio_bytes=%s\n" "$audio_bytes"
        printf "snapshot_bytes=%s\n" "$snapshot_bytes"
        printf "failures=%s\n" "${failures# }"
        [ -z "$failures" ]
    ' 2>&1
}

printf 'cycle\tstart_time\tduration_s\tprevious_boot_id\tboot_id\tresult\tfailures\n' >"$RESULTS"
log "GPIO cold-boot soak starting: cycles=$CYCLES off_ms=$OFF_TIME_MS target=$DEVICE_IP"

if ! mcp_initialize; then
    log "FATAL unable to initialize MCP"
    exit 2
fi

if ! mcp_tool lynx_power_capabilities '{}'; then
    log "FATAL unable to read power capabilities"
    exit 2
fi
capabilities="$MCP_REPLY"
power_profile=$(printf '%s\n' "$capabilities" | python3 -c '
import json, sys
outer = json.load(sys.stdin)
profile = json.loads(outer["result"]["content"][0]["text"])["profile"]
print("channel={},secondary={},mode={}".format(
    profile.get("channel"), profile.get("secondaryPowerChannel"), profile.get("roleMode")))
')
if [ "$power_profile" != "channel=4,secondary=5,mode=dualPower" ]; then
    log "FATAL unexpected power profile: $power_profile"
    exit 2
fi
log "Power profile verified: GPIO4+GPIO5 dualPower"

open_serial || log "WARN serial monitor unavailable at start; SSH/boot_id checks remain active"
previous_boot_id=$(remote_boot_id || true)
pass_count=0
fail_count=0

for cycle in $(seq 1 "$CYCLES"); do
    start_epoch=$(date +%s)
    start_time=$(date -Iseconds)
    serial_log="$LOG_DIR/serial/$(printf '%04d' "$cycle").log"
    health_log="$LOG_DIR/health/$(printf '%04d' "$cycle").log"
    : >"$serial_log"
    drain_serial "$serial_log" || true

    if mcp_tool lynx_power_control "{\"action\":\"cycle\",\"offTimeMs\":$OFF_TIME_MS}"; then
        power_json="$MCP_REPLY"
    else
        power_json=""
    fi
    power_text=$(printf '%s\n' "$power_json" | python3 -c '
import json, sys
outer = json.load(sys.stdin)
inner = json.loads(outer["result"]["content"][0]["text"])
print(inner.get("response", ""))
' 2>/dev/null || true)
    if ! printf '%s\n' "$power_text" | grep -q 'channels=4,5'; then
        duration=$(( $(date +%s) - start_epoch ))
        printf '%s\t%s\t%s\t%s\t\tFAIL\tpower:%s\n' "$cycle" "$start_time" "$duration" "$previous_boot_id" "${power_text:-no-response}" >>"$RESULTS"
        fail_count=$((fail_count + 1))
        log "[$cycle/$CYCLES] FAIL power control: ${power_text:-no-response}"
        sleep 2
        continue
    fi

    drain_serial "$serial_log" || true
    health=""
    deadline=$((start_epoch + BOOT_TIMEOUT_SECONDS))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        drain_serial "$serial_log" || true
        if health=$(remote_health); then
            break
        fi
        printf '%s\n' "$health" >"$health_log"
        sleep 2
    done
    drain_serial "$serial_log" || true
    printf '%s\n' "$health" >"$health_log"

    boot_id=$(printf '%s\n' "$health" | sed -n 's/^boot_id=//p' | tail -n 1)
    failures=$(printf '%s\n' "$health" | sed -n 's/^failures=//p' | tail -n 1)
    duration=$(( $(date +%s) - start_epoch ))
    result=PASS
    if [ -z "$boot_id" ]; then
        result=FAIL
        failures="${failures:+$failures }boot-timeout"
    elif [ -n "$previous_boot_id" ] && [ "$boot_id" = "$previous_boot_id" ]; then
        result=FAIL
        failures="${failures:+$failures }boot-id-unchanged"
    elif [ -n "$failures" ]; then
        result=FAIL
    fi

    if [ "$result" = PASS ]; then
        pass_count=$((pass_count + 1))
        log "[$cycle/$CYCLES] PASS ${duration}s boot_id=$boot_id"
    else
        fail_count=$((fail_count + 1))
        log "[$cycle/$CYCLES] FAIL ${duration}s boot_id=${boot_id:-none} failures=${failures:-unknown}"
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$cycle" "$start_time" "$duration" "$previous_boot_id" "$boot_id" "$result" "$failures" >>"$RESULTS"
    [ -n "$boot_id" ] && previous_boot_id="$boot_id"
done

log "GPIO cold-boot soak complete: total=$CYCLES pass=$pass_count fail=$fail_count"
printf 'total=%s\npass=%s\nfail=%s\nlog_dir=%s\n' "$CYCLES" "$pass_count" "$fail_count" "$LOG_DIR" >>"$SUMMARY"
[ "$fail_count" -eq 0 ]
