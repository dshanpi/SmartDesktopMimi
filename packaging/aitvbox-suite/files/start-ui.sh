#!/bin/sh
# Wait for backend IPC, then make boot-play release the framebuffer before
# starting LVGL.  lvglsim must never render while boot-play is still alive.

export HOME=/tmp
export LV_SETTINGS_FILE=/overlay/lv_port_linux_settings.bin
export LD_LIBRARY_PATH=/usr/lib

# Touch input: resolve the touch panel by name (gt9xxnew_ts by default;
# override with LV_TOUCH_INPUT_NAME) to /dev/input/eventX, then open it
# directly as a POINTER device. Resolving by name avoids depending on a
# fixed event node number, which can shift if other input devices probe.
TOUCH_NAME=${LV_TOUCH_INPUT_NAME:-gt9xxnew_ts}
IR_NAME=${LV_IR_INPUT_NAME:-ir_remote_libdriver}
resolve_input_device() {
	want=$1
	awk -v want="$want" '
		/^N: Name="/ { name=$0; sub(/^N: Name="/,"",name); sub(/".*/,"",name) }
		/^H: Handlers=/ && name==want {
			for (i=1;i<=NF;i++) if ($i ~ /^event[0-9]+$/) { print "/dev/input/" $i; exit }
		}
	' /proc/bus/input/devices
}

export BOOT_PROGRESS_FILE=${BOOT_PROGRESS_FILE:-/tmp/boot_progress}
BACKEND_SOCKET=${LV_BACKEND_SOCKET:-/tmp/lv_port_linux_backend.sock}
UI_LOG=/tmp/lvglsim.log
UI_PID=""

log_ui() {
	message="aitvbox-ui: $*"
	echo "$message" >>"$UI_LOG"
	echo "$message" >/dev/console 2>/dev/null || true
	logger -t aitvbox-ui "$*" 2>/dev/null || true
}

log_backend_tail() {
	[ -s /tmp/lv_backend.log ] || return 0
	log_ui "last lv_backend messages follow"
	tail -n 20 /tmp/lv_backend.log >>"$UI_LOG" 2>/dev/null || true
	tail -n 20 /tmp/lv_backend.log >/dev/console 2>/dev/null || true
}

boot_play_running() {
	pidof boot-play >/dev/null 2>&1
}

wait_boot_play_gone() {
	i=0
	while [ "$i" -lt "$1" ]; do
		if ! boot_play_running; then
			return 0
		fi
		sleep 0.1
		i=$((i + 1))
	done
	return 1
}

restore_boot_play() {
	echo 84 >"$BOOT_PROGRESS_FILE"
	boot_play_running && return 0
	if command -v boot-play >/dev/null 2>&1; then
		log_ui "restoring boot screen after UI failure"
		boot-play boot >>"$UI_LOG" 2>&1 &
	else
		log_ui "cannot restore boot screen: boot-play is unavailable"
	fi
}

stop_ui_child() {
	[ -z "$UI_PID" ] || kill "$UI_PID" 2>/dev/null || true
	exit 0
}

i=0
while [ "$i" -lt 300 ]; do
	if [ -S "$BACKEND_SOCKET" ] && pidof lv_backend >/dev/null 2>&1; then
		break
	fi
	sleep 0.1
	i=$((i + 1))
done

if [ ! -S "$BACKEND_SOCKET" ] || ! pidof lv_backend >/dev/null 2>&1; then
	log_ui "backend did not become ready within 30 seconds; keeping boot screen"
	log_backend_tail
	exit 1
fi

if [ ! -x /usr/bin/lvglsim ]; then
	log_ui "lvglsim is missing or not executable; keeping boot screen"
	exit 1
fi

if [ ! -c /dev/fb0 ]; then
	log_ui "/dev/fb0 is unavailable; keeping boot screen"
	exit 1
fi

echo 100 > "$BOOT_PROGRESS_FILE"
if ! wait_boot_play_gone 80; then
	killall boot-play 2>/dev/null
	wait_boot_play_gone 20
fi

if boot_play_running; then
	killall -9 boot-play 2>/dev/null
	wait_boot_play_gone 10
fi

if boot_play_running; then
	log_ui "boot-play did not exit; refusing to start lvglsim over it"
	exit 1
fi

# Resolve the touch device now (it should already be probed by this point
# in boot); retry briefly in case it is still coming up.
TOUCH_DEV=""
n=0
while [ "$n" -lt 5 ]; do
	TOUCH_DEV=$(resolve_input_device "$TOUCH_NAME")
	[ -n "$TOUCH_DEV" ] && [ -e "$TOUCH_DEV" ] && break
	sleep 1
	n=$((n + 1))
done
if [ -n "$TOUCH_DEV" ] && [ -e "$TOUCH_DEV" ]; then
	export LV_LINUX_EVDEV_POINTER_DEVICE="$TOUCH_DEV"
	log_ui "touch: $TOUCH_NAME -> $TOUCH_DEV"
else
	log_ui "touch device '$TOUCH_NAME' not found; starting without touch"
fi

# R818 infrared receiver: the kernel NEC decoder exposes semantic EV_KEY
# events on PH19. Resolve by name because event numbers move as USB devices
# probe, then let LVGL use it as a keypad for focus, Enter and global Back.
IR_DEV=$(resolve_input_device "$IR_NAME")
if [ -n "$IR_DEV" ] && [ -e "$IR_DEV" ]; then
	export LV_LINUX_EVDEV_KEYPAD_DEVICE="$IR_DEV"
	log_ui "infrared: $IR_NAME -> $IR_DEV"
else
	log_ui "infrared device '$IR_NAME' not found; starting without remote control"
fi

cd /tmp || {
	log_ui "cannot enter /tmp; restoring boot screen"
	restore_boot_play
	exit 1
}

log_ui "backend ready; starting lvglsim"
trap stop_ui_child TERM INT
/usr/bin/lvglsim >>"$UI_LOG" 2>&1 &
UI_PID=$!
wait "$UI_PID"
ui_status=$?
UI_PID=""
trap - TERM INT

log_ui "lvglsim exited with status $ui_status"
log_backend_tail
restore_boot_play

# A clean exit is still unexpected while the service is meant to be running.
# Return failure so procd applies the bounded respawn policy.
[ "$ui_status" -ne 0 ] || ui_status=1
exit "$ui_status"
