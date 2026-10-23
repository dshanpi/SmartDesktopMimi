package kvm

import (
	"encoding/base64"
	"github.com/pion/webrtc/v4"
	"github.com/rs/zerolog"
	"testing"
	"time"
)

func TestFailedOfferClosesSession(t *testing.T) {
	t.Setenv("AITVBOX_PLATFORM_MODE", "0")
	previous := config
	config = &Config{}
	defer func() { config = previous }()
	for name, offer := range map[string]string{"base64": "!", "json": base64.StdEncoding.EncodeToString([]byte("{")), "sdp": base64.StdEncoding.EncodeToString([]byte(`{"type":"offer","sdp":"invalid"}`))} {
		t.Run(name, func(t *testing.T) {
			before := hidQueueWorkers()
			logger := zerolog.Nop()
			s, e := newSession(SessionConfig{Logger: &logger})
			if e != nil {
				t.Fatal(e)
			}
			defer s.peerConnection.Close()
			if _, e = s.ExchangeOffer(offer); e == nil {
				t.Fatal("invalid offer accepted")
			}
			deadline := time.Now().Add(time.Second)
			for s.peerConnection.ConnectionState() != webrtc.PeerConnectionStateClosed && time.Now().Before(deadline) {
				time.Sleep(time.Millisecond)
			}
			if s.peerConnection.ConnectionState() != webrtc.PeerConnectionStateClosed {
				t.Error("failed offer left peer open")
			}
			if s.peerConnection.ConnectionState() == webrtc.PeerConnectionStateClosed {
				deadline = time.Now().Add(time.Second)
				for hidQueueWorkers() > before && time.Now().Before(deadline) {
					time.Sleep(time.Millisecond)
				}
				if hidQueueWorkers() != before {
					t.Error("failed offer retained HID workers")
				}
				sessionInput.Lock()
				closed := s.inputClosed
				sessionInput.Unlock()
				if !closed {
					t.Error("failed session still accepts input")
				}
			}
		})
	}
}
