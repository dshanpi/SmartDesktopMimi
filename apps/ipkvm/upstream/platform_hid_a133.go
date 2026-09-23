package kvm

import (
	"errors"
	"io"
	"os"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

const (
	defaultKeyboardPath = "/dev/hidg0"
	defaultMousePath    = "/dev/hidg1"
	compositeMousePath  = "/dev/hidg0"
	defaultHIDLockPath  = "/var/run/aitvbox/hid.lock"
	defaultAgentLock    = "/var/run/aitvbox/agent.lock"
	absoluteMouseCenter = 16384
	absoluteMouseMax    = 32767
	absoluteMouseStep   = 32
)

var platformHIDState = struct {
	sync.Mutex
	absValid bool
	absX     int
	absY     int
	buttons  uint8
	nudged   bool
}{}

// Browser sessions and the keyboard/mouse HID RPC queues can call into the
// platform HID backend concurrently. The filesystem lock coordinates with
// the separate MCP agent process; this mutex prevents our own goroutines from
// repeatedly contending on that lock and preserves report ordering.
var platformHIDOperationLock sync.Mutex

func platformHIDReady() bool {
	return deviceWritable(platformKeyboardPath()) &&
		deviceWritable(platformMousePath())
}

func platformKeyboardPath() string {
	return envOrDefault("AITVBOX_HID_KEYBOARD", defaultKeyboardPath)
}

func platformMousePath() string {
	return resolvePlatformMousePath(
		os.Getenv("AITVBOX_HID_MOUSE"),
		platformKeyboardPath(),
		deviceWritable(defaultMousePath),
	)
}

func resolvePlatformMousePath(configured, keyboard string, splitAvailable bool) string {
	if configured != "" {
		// Older A133 service files pointed both variables at hidg0. Prefer
		// the production split mouse endpoint when it is present, while
		// preserving an explicitly configured composite or test device.
		if configured == defaultKeyboardPath &&
			keyboard == defaultKeyboardPath && splitAvailable {
			return defaultMousePath
		}
		return configured
	}
	if splitAvailable {
		return defaultMousePath
	}
	return compositeMousePath
}

func deviceWritable(path string) bool {
	info, err := os.Stat(path)
	if err != nil {
		return false
	}
	return info.Mode()&os.ModeCharDevice != 0 || info.Mode().IsRegular()
}

func platformMouseButtons() uint8 {
	platformHIDState.Lock()
	defer platformHIDState.Unlock()
	return platformHIDState.buttons
}

func platformKeyboardReport(modifier uint8, keys []uint8) error {
	report := make([]byte, 8)
	report[0] = modifier
	copy(report[2:], keys)
	if platformCompositeHID() {
		report = append([]byte{2}, report...)
	}
	return withPlatformHIDLock(func() error {
		return writeHIDReport(platformKeyboardPath(), report)
	})
}

func platformRelMouseReport(dx, dy int8, buttons uint8, wheel int8) error {
	if platformAbsoluteMouse() {
		platformHIDState.Lock()
		if !platformHIDState.absValid {
			platformHIDState.absX = absoluteMouseCenter
			platformHIDState.absY = absoluteMouseCenter
			platformHIDState.absValid = true
		}
		platformHIDState.absX = clampAbsoluteMouseCoordinate(
			platformHIDState.absX + int(dx)*absoluteMouseStep,
		)
		platformHIDState.absY = clampAbsoluteMouseCoordinate(
			platformHIDState.absY + int(dy)*absoluteMouseStep,
		)
		x := platformHIDState.absX
		y := platformHIDState.absY
		platformHIDState.buttons = buttons
		platformHIDState.nudged = false
		platformHIDState.Unlock()

		return withPlatformHIDLock(func() error {
			if err := writeAbsoluteMouseReport(x, y, buttons); err != nil {
				return err
			}
			if wheel != 0 {
				return writeAbsoluteWheelReport(wheel)
			}
			return nil
		})
	}

	platformHIDState.Lock()
	previousButtons := platformHIDState.buttons
	if dx == 0 && dy == 0 && previousButtons == 0 && buttons != 0 {
		// This Windows host ignores a stationary button-only interrupt
		// report. Move one unit on press and undo it on release so a click is
		// observable without causing pointer drift.
		dx = 1
		platformHIDState.nudged = true
	} else if dx == 0 && dy == 0 && previousButtons != 0 && buttons == 0 &&
		platformHIDState.nudged {
		dx = -1
		platformHIDState.nudged = false
	}
	platformHIDState.absValid = false
	platformHIDState.buttons = buttons
	platformHIDState.Unlock()
	return withPlatformHIDLock(func() error {
		return writeRelativeMouseReport(dx, dy, buttons, wheel)
	})
}

func platformAbsMouseReport(x, y int, buttons uint8) error {
	if platformAbsoluteMouse() {
		x = clampAbsoluteMouseCoordinate(x)
		y = clampAbsoluteMouseCoordinate(y)
		platformHIDState.Lock()
		platformHIDState.absX = x
		platformHIDState.absY = y
		platformHIDState.absValid = true
		platformHIDState.buttons = buttons
		platformHIDState.nudged = false
		platformHIDState.Unlock()
		return withPlatformHIDLock(func() error {
			return writeAbsoluteMouseReport(x, y, buttons)
		})
	}

	platformHIDState.Lock()
	dx, dy := 0, 0
	if platformHIDState.absValid {
		dx = normalizedMouseDelta(x - platformHIDState.absX)
		dy = normalizedMouseDelta(y - platformHIDState.absY)
	}
	previousButtons := platformHIDState.buttons
	if dx == 0 && dy == 0 && previousButtons == 0 && buttons != 0 {
		dx = 1
		platformHIDState.nudged = true
	} else if dx == 0 && dy == 0 && previousButtons != 0 && buttons == 0 &&
		platformHIDState.nudged {
		dx = -1
		platformHIDState.nudged = false
	}
	platformHIDState.absX = x
	platformHIDState.absY = y
	platformHIDState.absValid = true
	platformHIDState.buttons = buttons
	platformHIDState.Unlock()

	return withPlatformHIDLock(func() error {
		for dx != 0 || dy != 0 {
			stepX := clampMouseDelta(dx)
			stepY := clampMouseDelta(dy)
			if err := writeRelativeMouseReport(int8(stepX), int8(stepY), buttons, 0); err != nil {
				return err
			}
			dx -= stepX
			dy -= stepY
			time.Sleep(time.Millisecond)
		}
		return writeRelativeMouseReport(0, 0, buttons, 0)
	})
}

func normalizedMouseDelta(delta int) int {
	if delta == 0 {
		return 0
	}
	scaled := delta / 128
	if scaled == 0 {
		if delta > 0 {
			return 1
		}
		return -1
	}
	return scaled
}

func clampMouseDelta(delta int) int {
	if delta > 127 {
		return 127
	}
	if delta < -127 {
		return -127
	}
	return delta
}

func writeRelativeMouseReport(dx, dy int8, buttons uint8, wheel int8) error {
	report := []byte{buttons, byte(dx), byte(dy), byte(wheel)}
	if platformCompositeHID() {
		report = append([]byte{1}, report...)
	}
	return writeHIDReport(
		platformMousePath(),
		report,
	)
}

func writeAbsoluteMouseReport(x, y int, buttons uint8) error {
	return writeHIDReport(platformMousePath(), []byte{
		1,
		buttons,
		byte(x),
		byte(x >> 8),
		byte(y),
		byte(y >> 8),
	})
}

func writeAbsoluteWheelReport(wheel int8) error {
	return writeHIDReport(platformMousePath(), []byte{2, byte(wheel)})
}

func clampAbsoluteMouseCoordinate(value int) int {
	if value < 0 {
		return 0
	}
	if value > absoluteMouseMax {
		return absoluteMouseMax
	}
	return value
}

func platformCompositeHID() bool {
	return platformKeyboardPath() == platformMousePath()
}

func platformAbsoluteMouse() bool {
	return !platformCompositeHID() &&
		strings.EqualFold(os.Getenv("AITVBOX_HID_MOUSE_MODE"), "absolute")
}

func writeHIDReport(path string, report []byte) error {
	// A blocking hidg write can sleep forever when the upstream USB host is
	// detached. O_NONBLOCK turns that state into EAGAIN. A pending interrupt
	// report also returns EAGAIN, so retry briefly instead of dropping normal
	// high-frequency pointer input.
	file, err := os.OpenFile(path, os.O_WRONLY|syscall.O_NONBLOCK, 0)
	if err != nil {
		return err
	}
	defer file.Close()
	busyRetries := 0
	for len(report) > 0 {
		written, writeErr := file.Write(report)
		if writeErr != nil {
			if errors.Is(writeErr, syscall.EAGAIN) || errors.Is(writeErr, syscall.EWOULDBLOCK) {
				busyRetries++
				if busyRetries >= 100 {
					return writeErr
				}
				time.Sleep(time.Millisecond)
				continue
			}
			return writeErr
		}
		busyRetries = 0
		if written == 0 {
			return io.ErrShortWrite
		}
		report = report[written:]
	}
	return nil
}

func withPlatformHIDLock(operation func() error) error {
	platformHIDOperationLock.Lock()
	defer platformHIDOperationLock.Unlock()

	if agentOwnsInput() {
		return errors.New("local MCP agent owns USB input")
	}
	lockPath := envOrDefault("AITVBOX_HID_LOCK", defaultHIDLockPath)
	if err := os.MkdirAll(dirName(lockPath), 0755); err != nil {
		return err
	}

	for attempt := 0; attempt < 50; attempt++ {
		err := os.Mkdir(lockPath, 0700)
		if err == nil {
			if writeErr := os.WriteFile(lockPath+"/pid", []byte(strconv.Itoa(os.Getpid())+"\n"), 0600); writeErr != nil {
				_ = os.RemoveAll(lockPath)
				return writeErr
			}
			defer os.RemoveAll(lockPath)
			return operation()
		}
		if !errors.Is(err, os.ErrExist) {
			return err
		}
		removeStaleHIDLock(lockPath)
		time.Sleep(10 * time.Millisecond)
	}
	return errors.New("USB HID controller is busy")
}

func removeStaleHIDLock(path string) {
	data, err := os.ReadFile(path + "/pid")
	if err != nil {
		removeAbandonedHIDLock(path)
		return
	}
	pid, err := strconv.Atoi(stringTrimSpace(data))
	if err != nil {
		removeAbandonedHIDLock(path)
		return
	}
	if !processAlive(pid) {
		_ = os.RemoveAll(path)
	}
}

func removeAbandonedHIDLock(path string) {
	info, err := os.Stat(path)
	if err == nil && time.Since(info.ModTime()) > 2*time.Second {
		_ = os.RemoveAll(path)
	}
}

func agentOwnsInput() bool {
	data, err := os.ReadFile(envOrDefault("AITVBOX_AGENT_LOCK", defaultAgentLock))
	if err != nil {
		return false
	}
	pid, err := strconv.Atoi(stringTrimSpace(data))
	return err == nil && processAlive(pid)
}

func platformReleaseHID() error {
	platformHIDState.Lock()
	nudged := platformHIDState.nudged
	absValid := platformHIDState.absValid
	absX := platformHIDState.absX
	absY := platformHIDState.absY
	platformHIDState.absValid = false
	platformHIDState.buttons = 0
	platformHIDState.nudged = false
	platformHIDState.Unlock()
	if !platformHIDReady() {
		return nil
	}
	return withPlatformHIDLock(func() error {
		keyboardReport := make([]byte, 8)
		if platformCompositeHID() {
			keyboardReport = append([]byte{2}, keyboardReport...)
		}
		keyboardErr := writeHIDReport(
			platformKeyboardPath(),
			keyboardReport,
		)
		var mouseErr error
		if platformAbsoluteMouse() {
			if !absValid {
				absX = absoluteMouseCenter
				absY = absoluteMouseCenter
			}
			mouseErr = writeAbsoluteMouseReport(absX, absY, 0)
		} else {
			releaseDX := int8(0)
			if nudged {
				releaseDX = -1
			}
			mouseErr = writeRelativeMouseReport(releaseDX, 0, 0, 0)
		}
		if keyboardErr != nil {
			return keyboardErr
		}
		return mouseErr
	})
}
