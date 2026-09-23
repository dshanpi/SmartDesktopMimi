package kvm

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

func configureTestHID(t *testing.T) (string, string) {
	t.Helper()
	platformHIDState.Lock()
	platformHIDState.absValid = false
	platformHIDState.absX = 0
	platformHIDState.absY = 0
	platformHIDState.buttons = 0
	platformHIDState.nudged = false
	platformHIDState.Unlock()
	root := t.TempDir()
	keyboard := filepath.Join(root, "keyboard")
	mouse := filepath.Join(root, "mouse")
	if err := os.WriteFile(keyboard, make([]byte, 8), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(mouse, make([]byte, 4), 0600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("AITVBOX_PLATFORM_MODE", "1")
	t.Setenv("AITVBOX_HID_KEYBOARD", keyboard)
	t.Setenv("AITVBOX_HID_MOUSE", mouse)
	t.Setenv("AITVBOX_HID_MOUSE_MODE", "relative")
	t.Setenv("AITVBOX_HID_LOCK", filepath.Join(root, "hid.lock"))
	t.Setenv("AITVBOX_AGENT_LOCK", filepath.Join(root, "agent.lock"))
	return keyboard, mouse
}

func TestPlatformHIDReports(t *testing.T) {
	keyboard, mouse := configureTestHID(t)

	if err := platformKeyboardReport(2, []uint8{4, 5}); err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(keyboard)
	if err != nil {
		t.Fatal(err)
	}
	want := []byte{2, 0, 4, 5, 0, 0, 0, 0}
	if string(got) != string(want) {
		t.Fatalf("keyboard report = %v, want %v", got, want)
	}

	if err := platformRelMouseReport(-7, 12, 1, -1); err != nil {
		t.Fatal(err)
	}
	got, err = os.ReadFile(mouse)
	if err != nil {
		t.Fatal(err)
	}
	want = []byte{1, 249, 12, 255}
	if string(got) != string(want) {
		t.Fatalf("mouse report = %v, want %v", got, want)
	}
}

func TestPlatformCompositeHIDReportIDs(t *testing.T) {
	platformHIDState.Lock()
	platformHIDState.absValid = false
	platformHIDState.buttons = 0
	platformHIDState.nudged = false
	platformHIDState.Unlock()
	root := t.TempDir()
	hid := filepath.Join(root, "hidg0")
	if err := os.WriteFile(hid, nil, 0600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("AITVBOX_HID_KEYBOARD", hid)
	t.Setenv("AITVBOX_HID_MOUSE", hid)
	t.Setenv("AITVBOX_HID_MOUSE_MODE", "relative")
	t.Setenv("AITVBOX_HID_LOCK", filepath.Join(root, "hid.lock"))
	t.Setenv("AITVBOX_AGENT_LOCK", filepath.Join(root, "agent.lock"))

	if err := platformKeyboardReport(2, []uint8{4}); err != nil {
		t.Fatal(err)
	}
	report, err := os.ReadFile(hid)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{2, 2, 0, 4, 0, 0, 0, 0, 0}; !bytes.Equal(report, expected) {
		t.Fatalf("keyboard report = %v, want %v", report, expected)
	}

	if err := os.Truncate(hid, 0); err != nil {
		t.Fatal(err)
	}
	if err := platformRelMouseReport(5, -3, 1, 0); err != nil {
		t.Fatal(err)
	}
	report, err = os.ReadFile(hid)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{1, 1, 5, 253, 0}; !bytes.Equal(report, expected) {
		t.Fatalf("mouse report = %v, want %v", report, expected)
	}

	if err := os.Truncate(hid, 0); err != nil {
		t.Fatal(err)
	}
	if err := platformAbsMouseReport(32767, 12345, 1); err != nil {
		t.Fatal(err)
	}
	report, err = os.ReadFile(hid)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{1, 1, 0, 0, 0}; !bytes.Equal(report, expected) {
		t.Fatalf("translated absolute mouse report = %v, want %v", report, expected)
	}
}

func TestPlatformAbsoluteMouseReports(t *testing.T) {
	_, mouse := configureTestHID(t)
	if err := os.Truncate(mouse, 6); err != nil {
		t.Fatal(err)
	}
	t.Setenv("AITVBOX_HID_MOUSE_MODE", "absolute")

	if err := platformAbsMouseReport(32767, 12345, 1); err != nil {
		t.Fatal(err)
	}
	report, err := os.ReadFile(mouse)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{1, 1, 255, 127, 57, 48}; !bytes.Equal(report, expected) {
		t.Fatalf("absolute mouse report = %v, want %v", report, expected)
	}

	if err := platformRelMouseReport(-1, 2, 0, -1); err != nil {
		t.Fatal(err)
	}
	report, err = os.ReadFile(mouse)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{2, 255}; !bytes.Equal(report[:2], expected) {
		t.Fatalf("absolute wheel report prefix = %v, want %v", report[:2], expected)
	}

	platformHIDState.Lock()
	x, y := platformHIDState.absX, platformHIDState.absY
	platformHIDState.Unlock()
	if x != 32735 || y != 12409 {
		t.Fatalf("relative fallback position = (%d, %d), want (32735, 12409)", x, y)
	}
}

func TestPlatformAbsoluteMouseClampsCoordinates(t *testing.T) {
	_, mouse := configureTestHID(t)
	if err := os.Truncate(mouse, 6); err != nil {
		t.Fatal(err)
	}
	t.Setenv("AITVBOX_HID_MOUSE_MODE", "absolute")

	if err := platformAbsMouseReport(-1, 50000, 0); err != nil {
		t.Fatal(err)
	}
	report, err := os.ReadFile(mouse)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{1, 0, 0, 0, 255, 127}; !bytes.Equal(report, expected) {
		t.Fatalf("clamped absolute report = %v, want %v", report, expected)
	}
}

func TestResolvePlatformMousePathMigratesLegacyA133Configuration(t *testing.T) {
	if got := resolvePlatformMousePath(
		defaultKeyboardPath, defaultKeyboardPath, true,
	); got != defaultMousePath {
		t.Fatalf("legacy split mouse path = %q, want %q", got, defaultMousePath)
	}
	if got := resolvePlatformMousePath(
		defaultKeyboardPath, defaultKeyboardPath, false,
	); got != compositeMousePath {
		t.Fatalf("legacy composite mouse path = %q, want %q", got, compositeMousePath)
	}
	if got := resolvePlatformMousePath(
		"/tmp/composite", "/tmp/composite", true,
	); got != "/tmp/composite" {
		t.Fatalf("explicit composite mouse path = %q", got)
	}
}

func TestPlatformStationaryClickUsesNetZeroNudge(t *testing.T) {
	_, mouse := configureTestHID(t)

	if err := platformRelMouseReport(0, 0, 1, 0); err != nil {
		t.Fatal(err)
	}
	report, err := os.ReadFile(mouse)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{1, 1, 0, 0}; !bytes.Equal(report, expected) {
		t.Fatalf("button-down report = %v, want %v", report, expected)
	}

	if err := platformRelMouseReport(0, 0, 0, 0); err != nil {
		t.Fatal(err)
	}
	report, err = os.ReadFile(mouse)
	if err != nil {
		t.Fatal(err)
	}
	if expected := []byte{0, 255, 0, 0}; !bytes.Equal(report, expected) {
		t.Fatalf("button-up report = %v, want %v", report, expected)
	}
}

func TestPlatformHIDRejectsLocalAgentOwner(t *testing.T) {
	configureTestHID(t)
	agentLock := os.Getenv("AITVBOX_AGENT_LOCK")
	if err := os.WriteFile(agentLock, []byte(strconv.Itoa(os.Getpid())+"\n"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := platformKeyboardReport(0, []uint8{4}); err == nil {
		t.Fatal("expected browser HID to be rejected while local agent owns input")
	}
}

func TestPlatformHIDDoesNotStealFreshIncompleteLock(t *testing.T) {
	configureTestHID(t)
	lock := os.Getenv("AITVBOX_HID_LOCK")
	if err := os.Mkdir(lock, 0700); err != nil {
		t.Fatal(err)
	}
	removeStaleHIDLock(lock)
	if _, err := os.Stat(lock); err != nil {
		t.Fatalf("fresh lock was removed: %v", err)
	}

	old := time.Now().Add(-3 * time.Second)
	if err := os.Chtimes(lock, old, old); err != nil {
		t.Fatal(err)
	}
	removeStaleHIDLock(lock)
	if _, err := os.Stat(lock); !os.IsNotExist(err) {
		t.Fatalf("abandoned lock was not removed: %v", err)
	}
}

func TestPlatformHIDSerializesConcurrentReports(t *testing.T) {
	_, mouse := configureTestHID(t)

	var waitGroup sync.WaitGroup
	for i := 0; i < 32; i++ {
		waitGroup.Add(1)
		go func(delta int8) {
			defer waitGroup.Done()
			if err := platformRelMouseReport(delta, 0, 0, 0); err != nil {
				t.Errorf("platformRelMouseReport(%d): %v", delta, err)
			}
		}(int8(i + 1))
	}
	waitGroup.Wait()

	report, err := os.ReadFile(mouse)
	if err != nil {
		t.Fatal(err)
	}
	if len(report) != 4 {
		t.Fatalf("mouse report length = %d, want 4", len(report))
	}
}

func TestProcessAliveRejectsZombie(t *testing.T) {
	command := exec.Command("/bin/sh", "-c", "exit 0")
	if err := command.Start(); err != nil {
		t.Fatal(err)
	}
	defer command.Wait()
	deadline := time.Now().Add(time.Second)
	for processAlive(command.Process.Pid) && time.Now().Before(deadline) {
		time.Sleep(10 * time.Millisecond)
	}
	if processAlive(command.Process.Pid) {
		t.Fatal("zombie child was reported as alive")
	}
}

func TestPlatformHDMIOwnership(t *testing.T) {
	root := t.TempDir()
	owner := filepath.Join(root, "hdmi.lock")
	t.Setenv("AITVBOX_HDMI_OWNER_FILE", owner)
	platformOwnsHDMI = false
	platformAgentHDMI = false
	t.Cleanup(func() {
		platformOwnsHDMI = false
		platformAgentHDMI = false
		_ = os.Remove(owner)
	})

	if err := platformAcquireHDMI(); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(owner); err != nil {
		t.Fatalf("owner file not created: %v", err)
	}
	platformReleaseHDMI()
	if _, err := os.Stat(owner); !os.IsNotExist(err) {
		t.Fatalf("owner file still exists: %v", err)
	}
}

func TestSharedCaptureDoesNotCreateExclusiveOwnerFile(t *testing.T) {
	root := t.TempDir()
	owner := filepath.Join(root, "hdmi.lock")
	t.Setenv("AITVBOX_HDMI_OWNER_FILE", owner)
	t.Setenv("AITVBOX_SHARED_CAPTURE", "1")
	platformOwnsHDMI = false
	platformAgentHDMI = false
	t.Cleanup(func() {
		platformOwnsHDMI = false
		platformAgentHDMI = false
	})

	if err := platformAcquireHDMI(); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(owner); !os.IsNotExist(err) {
		t.Fatalf("shared capture created legacy owner file: %v", err)
	}
	platformReleaseHDMI()
}

func TestAgentHDMIReservationRejectsBrowserVideo(t *testing.T) {
	root := t.TempDir()
	owner := filepath.Join(root, "hdmi.lock")
	t.Setenv("AITVBOX_HDMI_OWNER_FILE", owner)
	platformOwnsHDMI = false
	platformAgentHDMI = false
	platformVideoUsers = 0
	t.Cleanup(func() {
		platformOwnsHDMI = false
		platformAgentHDMI = false
		platformVideoUsers = 0
		_ = os.Remove(owner)
	})

	if err := platformReserveHDMIForAgent(); err != nil {
		t.Fatal(err)
	}
	if err := platformAcquireHDMI(); err == nil {
		t.Fatal("browser video acquired HDMI while Agent reservation was active")
	}
	platformReleaseHDMIForAgent()
	if err := platformAcquireHDMI(); err != nil {
		t.Fatal(err)
	}
	platformReleaseHDMI()
}

func TestPlatformVideoRemainsResidentBetweenSessions(t *testing.T) {
	root := t.TempDir()
	owner := filepath.Join(root, "hdmi.lock")
	t.Setenv("AITVBOX_HDMI_OWNER_FILE", owner)

	var actions []string
	platformVideoMu.Lock()
	previousAction := platformVideoAction
	platformVideoAction = func(action string) error {
		actions = append(actions, action)
		return nil
	}
	platformVideoUsers = 0
	platformVideoLive = false
	platformOwnsHDMI = false
	platformAgentHDMI = false
	platformVideoMu.Unlock()
	t.Cleanup(func() {
		platformVideoMu.Lock()
		platformVideoAction = previousAction
		platformVideoUsers = 0
		platformVideoLive = false
		platformOwnsHDMI = false
		platformAgentHDMI = false
		platformVideoMu.Unlock()
		_ = os.Remove(owner)
	})

	platformStartVideo()
	platformStopVideo()
	platformStartVideo()
	platformStopVideo()

	if got := strings.Join(actions, ","); got != "start_video,start_video" {
		t.Fatalf("session cycling actions = %q, want one start_video per session", got)
	}
	if !platformVideoLive || !platformOwnsHDMI {
		t.Fatal("capture or HDMI ownership was released while idle")
	}

	platformForceStopVideo()
	if got := strings.Join(actions, ","); got != "start_video,start_video,stop_video" {
		t.Fatalf("shutdown actions = %q, want one ordered stop", got)
	}
	if platformVideoLive || platformOwnsHDMI {
		t.Fatal("capture or HDMI ownership remained after forced shutdown")
	}
}

func TestPlatformWebRTCRejectsInvalidConfiguration(t *testing.T) {
	t.Setenv("AITVBOX_WEBRTC_UDP_PORT", "0")
	if err := startPlatformWebRTC(); err == nil {
		t.Fatal("expected invalid WebRTC UDP port to be rejected")
	}

	t.Setenv("AITVBOX_WEBRTC_UDP_PORT", "40000")
	t.Setenv("AITVBOX_WEBRTC_PUBLIC_IP", "not-an-ip")
	if err := startPlatformWebRTC(); err == nil {
		t.Fatal("expected invalid WebRTC public IP to be rejected")
	}
}
