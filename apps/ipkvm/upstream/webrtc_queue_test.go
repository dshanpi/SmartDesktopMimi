package kvm

import (
	"testing"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/webrtc/v4/pkg/media"
	"github.com/rs/zerolog"
)

func TestRTCPRequestsKeyFrameForPLIAndFIR(t *testing.T) {
	for name, packet := range map[string]rtcp.Packet{
		"pli": &rtcp.PictureLossIndication{},
		"fir": &rtcp.FullIntraRequest{},
	} {
		t.Run(name, func(t *testing.T) {
			if !rtcpRequestsKeyFrame([]rtcp.Packet{packet}) {
				t.Fatal("keyframe feedback was ignored")
			}
		})
	}
	if rtcpRequestsKeyFrame([]rtcp.Packet{&rtcp.ReceiverReport{}}) {
		t.Fatal("receiver report was mistaken for keyframe feedback")
	}
}

func TestVideoWriterStartsAtDecoderRecoveryPoint(t *testing.T) {
	session := &Session{}
	logger := zerolog.Nop()
	session.startVideoWriter(&logger)
	defer session.stopVideoWriter()

	if !session.videoNeedsKeyFrame.Load() {
		t.Fatal("new WebRTC session accepted dependent frames before its first IDR")
	}
}

func TestVideoSampleQueueDropsInsteadOfBlockingCapture(t *testing.T) {
	session := &Session{
		videoSampleQueue: make(chan queuedVideoSample, 1),
	}
	session.enqueueVideoSample(media.Sample{Data: []byte{1}}, false)

	done := make(chan struct{})
	go func() {
		session.enqueueVideoSample(media.Sample{Data: []byte{2}}, false)
		close(done)
	}()

	select {
	case <-done:
	case <-time.After(100 * time.Millisecond):
		t.Fatal("a full WebRTC queue blocked the capture caller")
	}
	if dropped := session.videoDroppedSamples.Load(); dropped != 1 {
		t.Fatalf("dropped samples = %d, want 1", dropped)
	}
}

func TestVideoSampleQueueKeyFrameReplacesStaleFrames(t *testing.T) {
	session := &Session{
		videoSampleQueue: make(chan queuedVideoSample, videoSampleQueueSize),
	}
	for index := 0; index < videoSampleQueueSize; index++ {
		session.enqueueVideoSample(
			media.Sample{Data: []byte{byte(index)}},
			false,
		)
	}

	session.enqueueVideoSample(media.Sample{Data: []byte{99}}, true)

	if queued := len(session.videoSampleQueue); queued != 1 {
		t.Fatalf("queued samples after IDR = %d, want 1", queued)
	}
	sample := <-session.videoSampleQueue
	if !sample.keyFrame || len(sample.sample.Data) != 1 ||
		sample.sample.Data[0] != 99 {
		t.Fatalf("queue did not retain the newest IDR: %#v", sample)
	}
}

func TestVideoSampleQueueWaitsForIDRAfterOverflow(t *testing.T) {
	session := &Session{
		videoSampleQueue: make(chan queuedVideoSample, 1),
	}
	session.enqueueVideoSample(media.Sample{Data: []byte{1}}, false)
	session.enqueueVideoSample(media.Sample{Data: []byte{2}}, false)

	if queued := len(session.videoSampleQueue); queued != 0 {
		t.Fatalf("queued samples after overflow = %d, want 0", queued)
	}
	if !session.videoNeedsKeyFrame.Load() {
		t.Fatal("overflow did not put the video queue into IDR recovery mode")
	}

	session.enqueueVideoSample(media.Sample{Data: []byte{3}}, false)
	if queued := len(session.videoSampleQueue); queued != 0 {
		t.Fatalf("queued dependent P-frames during recovery = %d, want 0", queued)
	}

	session.enqueueVideoSample(media.Sample{Data: []byte{4}}, true)
	if session.videoNeedsKeyFrame.Load() {
		t.Fatal("IDR did not clear video queue recovery mode")
	}
	recovery := <-session.videoSampleQueue
	if !recovery.keyFrame || recovery.sample.Data[0] != 4 {
		t.Fatalf("recovery sample = %#v, want the newest IDR", recovery)
	}

	session.enqueueVideoSample(media.Sample{Data: []byte{5}}, false)
	next := <-session.videoSampleQueue
	if next.keyFrame || next.sample.Data[0] != 5 {
		t.Fatalf("post-recovery sample = %#v, want a P-frame", next)
	}
	if dropped := session.videoDroppedSamples.Load(); dropped != 2 {
		t.Fatalf("dropped samples = %d, want 2", dropped)
	}
}
