package kvm

import (
	"bytes"
	"net"
	"os"
	"path/filepath"
	"testing"
)

func TestH264NALPresenceHandlesMixedAnnexBPrefixes(t *testing.T) {
	stream := []byte{
		0, 0, 0, 1, 0x67, 0x42, 0, 0x1f,
		0, 0, 1, 0x68, 0xce, 0x3c, 0x80,
		0, 0, 0, 1, 0x65, 0x88,
	}
	hasSPS, hasPPS, hasIDR := h264NALPresence(stream)
	if !hasSPS || !hasPPS || !hasIDR {
		t.Fatalf("NAL presence = SPS:%v PPS:%v IDR:%v", hasSPS, hasPPS, hasIDR)
	}
}

func TestH264ParameterSetsSurviveWebServiceRestart(t *testing.T) {
	cachePath := filepath.Join(t.TempDir(), "h264-params.bin")
	t.Setenv("AITVBOX_H264_PARAMETER_CACHE", cachePath)
	parameterSets := []byte{
		0, 0, 0, 1, 0x67, 0x42, 0, 0x1f,
		0, 0, 0, 1, 0x68, 0xce, 0x3c, 0x80,
	}

	keyframeLock.Lock()
	previous := cachedParameterSets
	cachedParameterSets = nil
	keyframeLock.Unlock()
	t.Cleanup(func() {
		keyframeLock.Lock()
		cachedParameterSets = previous
		keyframeLock.Unlock()
	})

	if err := setCachedH264ParameterSets(parameterSets, true); err != nil {
		t.Fatal(err)
	}
	keyframeLock.Lock()
	cachedParameterSets = nil
	keyframeLock.Unlock()
	if err := loadCachedH264ParameterSets(); err != nil {
		t.Fatal(err)
	}
	keyframeLock.Lock()
	got := append([]byte(nil), cachedParameterSets...)
	keyframeLock.Unlock()
	if !bytes.Equal(got, parameterSets) {
		t.Fatalf("reloaded parameter sets = %x, want %x", got, parameterSets)
	}
}

func TestOwnedUnixListenerDoesNotUnlinkReplacement(t *testing.T) {
	socketPath := filepath.Join(t.TempDir(), "video.sock")
	oldListener, err := listenOwnedUnix(socketPath)
	if err != nil {
		t.Fatalf("listen old socket: %v", err)
	}

	// Simulate a replacement process removing the old name and binding the
	// same path before the old process has completed its shutdown.
	if err := os.Remove(socketPath); err != nil {
		t.Fatalf("remove old socket path: %v", err)
	}
	newListener, err := listenOwnedUnix(socketPath)
	if err != nil {
		t.Fatalf("listen replacement socket: %v", err)
	}
	defer newListener.Close()

	if err := oldListener.Close(); err != nil {
		t.Fatalf("close old listener: %v", err)
	}
	if _, err := os.Lstat(socketPath); err != nil {
		t.Fatalf("old listener removed replacement socket: %v", err)
	}

	connection, err := net.Dial("unix", socketPath)
	if err != nil {
		t.Fatalf("replacement listener is unreachable: %v", err)
	}
	_ = connection.Close()

	if err := newListener.Close(); err != nil {
		t.Fatalf("close replacement listener: %v", err)
	}
	if _, err := os.Lstat(socketPath); !os.IsNotExist(err) {
		t.Fatalf("owned socket path was not removed, stat error: %v", err)
	}
}

func TestOwnedUnixListenerSocketPermissions(t *testing.T) {
	socketPath := filepath.Join(t.TempDir(), "control.sock")
	listener, err := listenOwnedUnix(socketPath)
	if err != nil {
		t.Fatalf("listen socket: %v", err)
	}
	defer listener.Close()

	info, err := os.Lstat(socketPath)
	if err != nil {
		t.Fatalf("stat socket: %v", err)
	}
	if got := info.Mode().Perm(); got != 0600 {
		t.Fatalf("socket permissions = %04o, want 0600", got)
	}
}
