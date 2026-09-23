package kvm

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"net"
	"sync"
	"testing"
	"time"
)

func TestControlSocketHandlesConcurrentStreamResponses(t *testing.T) {
	server, client := net.Pipe()
	handlerDone := make(chan struct{})
	go func() {
		handleCtrlClient(server)
		close(handlerDone)
	}()
	deadline := time.Now().Add(time.Second)
	for {
		ctrlConnectionMu.RLock()
		connected := ctrlSocketConn == server
		ctrlConnectionMu.RUnlock()
		if connected {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("control socket handler did not register its connection")
		}
		time.Sleep(time.Millisecond)
	}
	t.Cleanup(func() {
		_ = client.Close()
		select {
		case <-handlerDone:
		case <-time.After(time.Second):
			t.Fatal("control socket handler did not stop")
		}
		requestMu.Lock()
		ongoingRequests = make(map[int32]chan *CtrlResponse)
		requestMu.Unlock()
	})

	type result struct {
		response *CtrlResponse
		err      error
	}
	results := make(chan result, 2)
	for _, action := range []string{"first", "second"} {
		action := action
		go func() {
			response, err := CallCtrlAction(action, nil)
			results <- result{response: response, err: err}
		}()
	}

	decoder := json.NewDecoder(client)
	requests := make([]CtrlAction, 2)
	for index := range requests {
		if err := decoder.Decode(&requests[index]); err != nil {
			t.Fatal(err)
		}
	}
	first, err := json.Marshal(CtrlResponse{
		Seq: requests[0].Seq, Result: map[string]interface{}{"ok": true},
	})
	if err != nil {
		t.Fatal(err)
	}
	second, err := json.Marshal(CtrlResponse{
		Seq: requests[1].Seq, Result: map[string]interface{}{"ok": true},
	})
	if err != nil {
		t.Fatal(err)
	}
	combined := append(append(first, '\n'), second...)
	midpoint := len(combined) / 2
	if _, err := client.Write(combined[:midpoint]); err != nil {
		t.Fatal(err)
	}
	if _, err := client.Write(combined[midpoint:]); err != nil {
		t.Fatal(err)
	}

	for range requests {
		select {
		case result := <-results:
			if result.err != nil || result.response == nil {
				t.Fatalf("control request failed: response=%#v err=%v",
					result.response, result.err)
			}
		case <-time.After(time.Second):
			t.Fatal("control request timed out")
		}
	}
	requestMu.Lock()
	pending := len(ongoingRequests)
	requestMu.Unlock()
	if pending != 0 {
		t.Fatalf("pending request count = %d, want 0", pending)
	}
}

func TestVideoBroadcasterCallbacksRunWithoutLock(t *testing.T) {
	broadcaster := &VideoBroadcaster{
		subscribers: make(map[string]chan []byte),
	}
	var callbacks sync.WaitGroup
	callbacks.Add(2)
	broadcaster.onFirstSubscribe = func() {
		broadcaster.Broadcast([]byte("frame"))
		callbacks.Done()
	}
	broadcaster.onLastUnsubscribe = func() {
		broadcaster.Broadcast([]byte("frame"))
		callbacks.Done()
	}

	done := make(chan struct{})
	go func() {
		id, _ := broadcaster.Subscribe()
		broadcaster.Unsubscribe(id)
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("video broadcaster callback deadlocked")
	}
	callbacks.Wait()
}

func TestVideoTimestampClockUsesDefaultForFirstFrame(t *testing.T) {
	clock := videoTimestampClock{}
	if got := clock.Next(1_000_000); got != defaultVideoSampleDuration {
		t.Fatalf("first frame produced duration %s, want %s",
			got, defaultVideoSampleDuration)
	}
}

func TestVideoTimestampClockUsesCaptureTimeline(t *testing.T) {
	clock := videoTimestampClock{}
	_ = clock.Next(1_000_000)
	if got := clock.Next(1_016_667); got != 16_667*time.Microsecond {
		t.Fatalf("capture timestamp produced duration %s", got)
	}
}

func TestVideoTimestampClockIgnoresIdleGap(t *testing.T) {
	clock := videoTimestampClock{}
	_ = clock.Next(1_000_000)
	if got := clock.Next(1_000_000 + uint64((45*time.Minute)/time.Microsecond)); got != defaultVideoSampleDuration {
		t.Fatalf("idle gap produced duration %s, want %s",
			got, defaultVideoSampleDuration)
	}
}

func TestReadVersionedVideoFrameHeader(t *testing.T) {
	header := make([]byte, 16)
	copy(header, videoFrameMagic)
	binary.LittleEndian.PutUint32(header[4:8], 1234)
	binary.LittleEndian.PutUint64(header[8:16], 9_876_543)
	length, timestamp, err := readVideoFrameHeader(bytes.NewReader(header))
	if err != nil {
		t.Fatal(err)
	}
	if length != 1234 || timestamp != 9_876_543 {
		t.Fatalf("header = (%d, %d), want (1234, 9876543)", length, timestamp)
	}
}

func TestReadLegacyVideoFrameHeader(t *testing.T) {
	header := make([]byte, 4)
	binary.LittleEndian.PutUint32(header, 4321)
	length, timestamp, err := readVideoFrameHeader(bytes.NewReader(header))
	if err != nil {
		t.Fatal(err)
	}
	if length != 4321 || timestamp != 0 {
		t.Fatalf("legacy header = (%d, %d), want (4321, 0)",
			length, timestamp)
	}
}
