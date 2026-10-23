package kvm

import (
	"bytes"
	"github.com/rs/zerolog"
	"kvm/internal/hidrpc"
	"os"
	"testing"
	"time"
)

func TestPeerCloseReleasesHeldInput(t *testing.T) {
	keyboard, mouse := configureTestHID(t)
	// Construct without platform ICE mux; no offer, traffic or real HID device.
	t.Setenv("AITVBOX_PLATFORM_MODE", "0")
	previous := config
	config = &Config{}
	defer func() { config = previous }()
	logger := zerolog.Nop()
	s, e := newSession(SessionConfig{Logger: &logger})
	if e != nil {
		t.Fatal(e)
	}
	defer s.peerConnection.Close()
	t.Setenv("AITVBOX_PLATFORM_MODE", "1")
	press := func(data []byte) {
		var m hidrpc.Message
		if e := hidrpc.Unmarshal(data, &m); e != nil {
			t.Fatal(e)
		}
		handleHidRPCMessage(m, s)
	}
	press([]byte{byte(hidrpc.TypeKeyboardReport), 2, 4})
	press([]byte{byte(hidrpc.TypeMouseReport), 1, 0, 1})
	b, e := os.ReadFile(keyboard)
	if e != nil || b[0] != 2 || b[2] != 4 {
		t.Fatalf("keyboard press failed: %v %v", b, e)
	}
	b, e = os.ReadFile(mouse)
	if e != nil || b[0] != 1 {
		t.Fatalf("mouse press failed: %v %v", b, e)
	}
	if e := s.peerConnection.Close(); e != nil {
		t.Fatal(e)
	}
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		k, _ := os.ReadFile(keyboard)
		m, _ := os.ReadFile(mouse)
		if bytes.Equal(k, make([]byte, 8)) && len(m) > 0 && m[0] == 0 {
			return
		}
		time.Sleep(time.Millisecond)
	}
	k, _ := os.ReadFile(keyboard)
	m, _ := os.ReadFile(mouse)
	t.Fatalf("closed peer retained pressed input: keyboard=%v mouse=%v", k, m)
}
