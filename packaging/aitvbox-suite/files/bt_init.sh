#!/bin/sh

# Compatibility entry point invoked synchronously by Allwinner
# btmanager-v4.0.  Never re-enter the procd S96 initializer after hci0 is
# ready: its long-lived hciattach instance keeps the init service active and a
# nested `start` waits forever on rc.common's flock, freezing lv_backend.

hci0_is_up() {
	[ -d /sys/class/bluetooth/hci0 ] || return 1
	hciconfig hci0 2>/dev/null | grep -q "UP"
}

case "${1:-start}" in
start)
	if ! hci0_is_up; then
		[ -x /etc/init.d/bluetooth_init ] || exit 1
		# Run the worker directly.  Calling the procd `start` action here can
		# deadlock with the already queued S96 instance.
		/etc/init.d/bluetooth_init worker || exit 1
	fi

	if ! pidof bluetoothd >/dev/null 2>&1; then
		[ -x /etc/init.d/aitvbox-bluetooth ] || exit 1
		/etc/init.d/aitvbox-bluetooth start || exit 1
	fi
	;;
stop)
	# bluetoothd and the UART controller are product-level services owned by
	# procd.  btmanager only owns its profiles/bluealsa child; do not tear down
	# the shared controller when a client deinitializes.
	;;
*)
	echo "usage: $0 {start|stop}" >&2
	exit 2
	;;
esac

exit 0
