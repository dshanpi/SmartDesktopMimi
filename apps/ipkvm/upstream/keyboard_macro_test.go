package kvm

import (
	"context"
	"errors"
	"kvm/internal/hidrpc"
	"sync"
	"testing"
	"time"
)

func macroStep(key byte) []hidrpc.KeyboardMacroStep {
	return []hidrpc.KeyboardMacroStep{{Keys: []byte{key}, Delay: 2000}}
}
func awaitMacro(t *testing.T, ch <-chan error) error {
	t.Helper()
	select {
	case e := <-ch:
		return e
	case <-time.After(time.Second):
		t.Fatal("macro did not finish")
		return nil
	}
}
func waitRun(t *testing.T, c *keyboardMacroController, old *keyboardMacroRun) *keyboardMacroRun {
	t.Helper()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		c.mu.Lock()
		r := c.current
		c.mu.Unlock()
		if r != nil && r != old {
			return r
		}
		time.Sleep(time.Millisecond)
	}
	t.Fatal("replacement did not register")
	return nil
}
func TestKeyboardMacroReplacementOwnership(t *testing.T) {
	var c keyboardMacroController
	entered := make(chan struct{})
	release := make(chan struct{})
	second := make(chan struct{})
	var mu sync.Mutex
	var sequence []byte
	report := func(_ uint8, keys []uint8) error {
		key := keys[0]
		mu.Lock()
		sequence = append(sequence, key)
		mu.Unlock()
		if key == 4 {
			close(entered)
			<-release
		}
		if key == 5 {
			close(second)
		}
		return nil
	}
	a, b := make(chan error, 1), make(chan error, 1)
	go func() { a <- c.execute(macroStep(4), report) }()
	<-entered
	old := waitRun(t, &c, nil)
	go func() { b <- c.execute(macroStep(5), report) }()
	waitRun(t, &c, old)
	select {
	case <-second:
		t.Fatal("replacement emitted before old cleanup")
	default:
	}
	close(release)
	if e := awaitMacro(t, a); !errors.Is(e, context.Canceled) {
		t.Fatalf("old=%v", e)
	}
	select {
	case <-second:
	case <-time.After(time.Second):
		t.Fatal("replacement never started")
	}
	c.cancel()
	if e := awaitMacro(t, b); !errors.Is(e, context.Canceled) {
		t.Fatalf("replacement=%v", e)
	}
	mu.Lock()
	defer mu.Unlock()
	if len(sequence) != 4 || sequence[0] != 4 || sequence[1] != 0 || sequence[2] != 5 || sequence[3] != 0 {
		t.Fatalf("report order=%v", sequence)
	}
}
func TestKeyboardMacroCancelQueuedReplacement(t *testing.T) {
	var c keyboardMacroController
	entered := make(chan struct{})
	release := make(chan struct{})
	var sequence []byte
	report := func(_ uint8, k []uint8) error {
		sequence = append(sequence, k[0])
		if k[0] == 4 {
			close(entered)
			<-release
		}
		return nil
	}
	a, b := make(chan error, 1), make(chan error, 1)
	go func() { a <- c.execute(macroStep(4), report) }()
	<-entered
	old := waitRun(t, &c, nil)
	go func() { b <- c.execute(macroStep(5), report) }()
	waitRun(t, &c, old)
	c.cancel()
	close(release)
	if e := awaitMacro(t, a); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	if e := awaitMacro(t, b); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	if len(sequence) != 2 || sequence[0] != 4 || sequence[1] != 0 {
		t.Fatalf("cancelled queued macro emitted: %v", sequence)
	}
}
func TestKeyboardMacroCleanupAndErrors(t *testing.T) {
	writeErr := errors.New("write failed")
	releaseErr := errors.New("release failed")
	for _, mode := range []string{"normal", "write-error", "release-error", "both-errors", "pre-cancel"} {
		t.Run(mode, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			if mode == "pre-cancel" {
				cancel()
			}
			var calls int
			err := executeKeyboardMacroSteps(ctx, []hidrpc.KeyboardMacroStep{{Keys: []byte{4}}}, func(_ uint8, k []uint8) error {
				calls++
				if k[0] == 4 && (mode == "write-error" || mode == "both-errors") {
					return writeErr
				}
				if k[0] == 0 && (mode == "release-error" || mode == "both-errors") {
					return releaseErr
				}
				return nil
			})
			if mode == "pre-cancel" {
				if calls != 0 || !errors.Is(err, context.Canceled) {
					t.Fatalf("calls=%d err=%v", calls, err)
				}
				return
			}
			if calls != 2 {
				t.Fatalf("cleanup missing: %d", calls)
			}
			if errors.Is(err, writeErr) != (mode == "write-error" || mode == "both-errors") {
				t.Fatalf("write err=%v", err)
			}
			if errors.Is(err, releaseErr) != (mode == "release-error" || mode == "both-errors") {
				t.Fatalf("release err=%v", err)
			}
		})
	}
}
