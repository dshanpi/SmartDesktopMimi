#!/bin/sh
# Mount TF card for video (and other media) when the block device is present.
#
# Hardware note: this board only enumerates the TF slot after reboot (no hotplug).
# So we only need a quiet, idempotent boot-time mount — never fail the boot path.
#
# Usage:
#   aitvbox-mount-sdcard [start|stop]
#     start (default): mount if node exists and not already mounted
#     stop:            best-effort umount

DEV_DISK="${AITVBOX_SD_DISK:-/dev/mmcblk1}"
DEV_PART="${AITVBOX_SD_PART:-/dev/mmcblk1p1}"
MNT="${AITVBOX_SD_MNT:-/mnt/SDCARD}"
LOG="${AITVBOX_SD_LOG:-/tmp/aitvbox-sdcard.log}"

log() {
	echo "[aitvbox-sdcard] $*" >>"$LOG"
	# Keep stdout quiet when invoked from backend system(); verbose with -v or tty.
	if [ -n "$AITVBOX_SD_VERBOSE" ] || [ -t 1 ]; then
		echo "[aitvbox-sdcard] $*"
	fi
}

is_mounted() {
	grep -q " ${MNT} " /proc/mounts 2>/dev/null
}

do_stop() {
	if is_mounted; then
		umount "$MNT" 2>>"$LOG" || umount -l "$MNT" 2>>"$LOG" || true
		log "umount $MNT"
	fi
	return 0
}

try_mount() {
	local fstype="$1"
	shift
	mount -t "$fstype" "$@" "$DEV_PART" "$MNT" 2>>"$LOG"
}

do_start() {
	mkdir -p "$MNT"

	# No card / not probed yet (common when slot empty after reboot).
	if [ ! -b "$DEV_PART" ]; then
		if [ ! -b "$DEV_DISK" ]; then
			log "skip: no TF device ($DEV_DISK)"
		else
			log "skip: disk present but no partition ($DEV_PART)"
		fi
		return 0
	fi

	if is_mounted; then
		log "already mounted: $MNT"
		return 0
	fi

	# Kernel module may be auto-probed on mount; load early for clearer logs.
	modprobe exfat 2>>"$LOG" || true

	# Prefer exFAT (common on large cards), then FAT/NTFS.
	if try_mount exfat -o iocharset=utf8,errors=remount-ro; then
		log "mounted exfat $DEV_PART -> $MNT"
		return 0
	fi
	if try_mount vfat -o utf8,iocharset=utf8,codepage=936,errors=continue; then
		log "mounted vfat $DEV_PART -> $MNT"
		return 0
	fi
	if try_mount ntfs -o iocharset=utf8,utf8; then
		log "mounted ntfs $DEV_PART -> $MNT"
		return 0
	fi
	# Last resort: kernel type guess (may still fail without matching FS).
	if mount "$DEV_PART" "$MNT" 2>>"$LOG"; then
		log "mounted auto $DEV_PART -> $MNT"
		return 0
	fi

	log "ERROR: mount failed for $DEV_PART (exfat/vfat/ntfs)"
	return 0
}

cmd="${1:-start}"
case "$cmd" in
	start|mount)
		do_start
		;;
	stop|umount)
		do_stop
		;;
	*)
		echo "usage: $0 [start|stop]" >&2
		exit 2
		;;
esac
exit 0
