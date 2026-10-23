package kvm

import (
	"github.com/pion/webrtc/v4"
	"github.com/rs/zerolog"
	"kvm/internal/hidrpc"
	"runtime"
	"strings"
	"testing"
	"time"
)

func hidQueueWorkers() int {
	b := make([]byte, 1<<20)
	n := runtime.Stack(b, true)
	return strings.Count(string(b[:n]), "kvm.(*Session).handleQueues(")
}
func TestPeerCloseStopsHIDQueueWorkers(t *testing.T) {
	t.Setenv("AITVBOX_PLATFORM_MODE", "0")
	previous := config
	config = &Config{}
	defer func() { config = previous }()
	before := hidQueueWorkers()
	logger := zerolog.Nop()
	s, e := newSession(SessionConfig{Logger: &logger})
	if e != nil {
		t.Fatal(e)
	}
	// No offer, no ICE gathering, no media or device access.
	defer s.peerConnection.Close()
	// Legacy implementation leaves channels open; this test-only final cleanup
	// prevents the negative reproduction from leaking across other tests.
	defer func() {
		for _, q := range s.hidQueue {
			close(q)
		}
	}()
	deadline := time.Now().Add(time.Second)
	for hidQueueWorkers() < before+4 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if hidQueueWorkers() != before+4 {
		t.Fatal("four queue workers did not start")
	}
	if e := s.peerConnection.Close(); e != nil {
		t.Fatal(e)
	}
	deadline = time.Now().Add(time.Second)
	for hidQueueWorkers() > before && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if got := hidQueueWorkers(); got != before {
		t.Fatalf("peer closed but HID workers remain: before=%d after=%d", before, got)
	}
}

func TestHIDQueueStopUnblocksProducer(t *testing.T) {
	s := &Session{}
	s.initQueues()
	logger := zerolog.Nop()
	q := s.hidQueue[1]
	for i := 0; i < cap(q); i++ {
		q <- hidQueueMessage{}
	}
	handler := getOnHidMessageHandler(s, &logger, "hidrpc")
	done := make(chan struct{})
	go func() {
		handler(webrtc.DataChannelMessage{Data: []byte{byte(hidrpc.TypeKeyboardReport), 0}})
		close(done)
	}()
	select {
	case <-done:
		t.Fatal("full queue did not block")
	case <-time.After(20 * time.Millisecond):
	}
	s.stopHIDQueues()
	s.stopHIDQueues()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("closed queue kept producer blocked")
	}
	before := len(q)
	handler(webrtc.DataChannelMessage{Data: []byte{byte(hidrpc.TypeKeyboardReport), 0}})
	if len(q) != before {
		t.Fatal("post-close message was queued")
	}
}
func TestHIDQueueStopSkipsBufferedInput(t *testing.T) {
	s := &Session{}
	s.initQueues()
	// Handshake would set availability if dispatched.
	s.hidQueue[0] <- hidQueueMessage{DataChannelMessage: webrtc.DataChannelMessage{Data: []byte{byte(hidrpc.TypeHandshake), 1}}}
	s.stopHIDQueues()
	done := make(chan struct{})
	go func() { s.handleQueues(0); close(done) }()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("consumer did not stop")
	}
	if s.hidRPCAvailable {
		t.Fatal("buffered input ran after stop")
	}
}
