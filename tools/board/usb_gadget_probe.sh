#!/bin/sh
# Short, self-restoring board diagnostic for the A133 USB device controller.
# It always returns the gadget to its original ADB-only configuration.

set -u

mode=${1:-keyboard}
gadget=/sys/kernel/config/usb_gadget/g1
config=$gadget/configs/c.1
log=/tmp/aitvbox-usb-probe.log
udc=$(cat "$gadget/UDC")
[ -n "$udc" ] || udc=$(ls /sys/class/udc | head -n 1)

restore_adb() {
    echo "" >"$gadget/UDC" 2>/dev/null || true
    rm -f "$config/hid.keyboard" "$config/hid.mouse" \
        "$config/hid.composite" "$config/ffs.adb"
    ln -s "$gadget/functions/ffs.adb" "$config/ffs.adb"
    echo "$udc" >"$gadget/UDC"
}

{
    echo "mode=$mode udc=$udc"
    echo "" >"$gadget/UDC" 2>/dev/null || true
    rm -f "$config/hid.keyboard" "$config/hid.mouse" \
        "$config/hid.composite" "$config/ffs.adb"
    case "$mode" in
        keyboard)
            ln -s "$gadget/functions/hid.keyboard" "$config/hid.keyboard"
            ;;
        mouse)
            ln -s "$gadget/functions/hid.mouse" "$config/hid.mouse"
            ;;
        hid)
            ln -s "$gadget/functions/hid.keyboard" "$config/hid.keyboard"
            ln -s "$gadget/functions/hid.mouse" "$config/hid.mouse"
            ;;
        adb-keyboard)
            ln -s "$gadget/functions/ffs.adb" "$config/ffs.adb"
            ln -s "$gadget/functions/hid.keyboard" "$config/hid.keyboard"
            ;;
        adb-mouse)
            ln -s "$gadget/functions/ffs.adb" "$config/ffs.adb"
            ln -s "$gadget/functions/hid.mouse" "$config/hid.mouse"
            ;;
        adb-composite)
            rmdir "$gadget/functions/hid.keyboard" \
                "$gadget/functions/hid.mouse" 2>/dev/null || true
            composite="$gadget/functions/hid.composite"
            mkdir -p "$composite"
            echo 0 >"$composite/protocol"
            echo 0 >"$composite/subclass"
            echo 9 >"$composite/report_length"
            {
                printf '\005\001\011\006\241\001\205\001\005\007\031\340\051\347\025\000\045\001\165\001\225\010\201\002\225\001\165\010\201\001\225\005\165\001\005\010\031\001\051\005\221\002\225\001\165\003\221\001\225\006\165\010\025\000\045\145\005\007\031\000\051\145\201\000\300'
                printf '\005\001\011\002\241\001\205\002\011\001\241\000\005\011\031\001\051\003\025\000\045\001\225\003\165\001\201\002\225\001\165\005\201\001\005\001\011\060\011\061\011\070\025\201\045\177\165\010\225\003\201\006\300\300'
            } >"$composite/report_desc"
            ln -s "$gadget/functions/ffs.adb" "$config/ffs.adb"
            ln -s "$composite" "$config/hid.composite"
            ;;
        *)
            echo "unsupported mode"
            restore_adb
            exit 2
            ;;
    esac
    if echo "$udc" >"$gadget/UDC"; then
        echo "probe_bind=ok"
        ls -l /dev/hidg* 2>&1 || true
    else
        echo "probe_bind=failed status=$?"
    fi
    sleep 5
    if restore_adb; then
        echo "restore_adb=ok"
    else
        echo "restore_adb=failed status=$?"
    fi
} >"$log" 2>&1
