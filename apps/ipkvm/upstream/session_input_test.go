package kvm

import (
	"bytes"
	"context"
	"errors"
	"os"
	"testing"
	"time"
)

func checkInputBytes(t *testing.T, path string, want []byte) {
	t.Helper()
	got, e := os.ReadFile(path)
	if e != nil || !bytes.Equal(got, want) {
		t.Fatalf("report=%v expected=%v err=%v", got, want, e)
	}
}
func TestSessionInputOwnershipAndLateWrites(t *testing.T) {
	k, m := configureTestHID(t)
	a, b := &Session{}, &Session{}
	if e := a.keyboardReport(2, []byte{4}); e != nil {
		t.Fatal(e)
	}
	if e := a.withHID(true, func() error { return rpcRelMouseReport(1, 0, 1) }); e != nil {
		t.Fatal(e)
	}
	if e := b.keyboardReport(1, []byte{5}); e != nil {
		t.Fatal(e)
	}
	if e := b.withHID(true, func() error { return rpcRelMouseReport(2, 0, 2) }); e != nil {
		t.Fatal(e)
	}
	a.closeHIDInput()
	a.closeHIDInput()
	if e := a.keyboardReport(8, []byte{6}); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	if e := a.withHID(true, func() error { return rpcRelMouseReport(3, 0, 1) }); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	checkInputBytes(t, k, []byte{1, 0, 5, 0, 0, 0, 0, 0})
	checkInputBytes(t, m, []byte{2, 2, 0, 0})
	b.closeHIDInput()
	checkInputBytes(t, k, make([]byte, 8))
	checkInputBytes(t, m, make([]byte, 4))
}
func TestSessionCloseOrdersAfterInflightWrite(t *testing.T) {
	k, _ := configureTestHID(t)
	s := &Session{}
	entered, release, closed := make(chan struct{}), make(chan struct{}), make(chan struct{})
	done := make(chan error, 1)
	go func() {
		done <- s.withHID(false, func() error { close(entered); <-release; return rpcKeyboardReport(2, []byte{4}) })
	}()
	<-entered
	go func() { s.closeHIDInput(); close(closed) }()
	select {
	case <-closed:
		t.Fatal("close overtook write")
	case <-time.After(20 * time.Millisecond):
	}
	close(release)
	if e := <-done; e != nil {
		t.Fatal(e)
	}
	select {
	case <-closed:
	case <-time.After(time.Second):
		t.Fatal("close stuck")
	}
	checkInputBytes(t, k, make([]byte, 8))
}
func TestSessionCloseCancelsOnlyOwnedMacro(t *testing.T) {
	k, _ := configureTestHID(t)
	s, other := &Session{}, &Session{}
	done := make(chan error, 1)
	go func() { done <- s.executeKeyboardMacro(macroStep(4)) }()
	deadline := time.Now().Add(time.Second)
	for {
		b, _ := os.ReadFile(k)
		if len(b) == 8 && b[2] == 4 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("macro never wrote")
		}
		time.Sleep(time.Millisecond)
	}
	keyboardMacros.cancelOwned(other)
	other.closeHIDInput()
	select {
	case e := <-done:
		t.Fatalf("unrelated session canceled macro: %v", e)
	case <-time.After(20 * time.Millisecond):
	}
	s.closeHIDInput()
	if e := awaitMacro(t, done); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	checkInputBytes(t, k, make([]byte, 8))
	if e := s.executeKeyboardMacro(macroStep(5)); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	checkInputBytes(t, k, make([]byte, 8))
}
func TestSessionReplacementRevokesPreviousInput(t *testing.T) {
	k, _ := configureTestHID(t)
	old := currentSession.Swap(nil)
	defer currentSession.Store(old)
	a, b := &Session{}, &Session{}
	a.initQueues()
	b.initQueues()
	replaceCurrentSession(a)
	if e := a.keyboardReport(2, []byte{4}); e != nil {
		t.Fatal(e)
	}
	if replaceCurrentSession(b) != a {
		t.Fatal("wrong previous")
	}
	checkInputBytes(t, k, make([]byte, 8))
	if e := a.keyboardReport(2, []byte{4}); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	if e := b.keyboardReport(1, []byte{5}); e != nil {
		t.Fatal(e)
	}
	a.stopHIDQueues()
	checkInputBytes(t, k, []byte{1, 0, 5, 0, 0, 0, 0, 0})
	b.stopHIDQueues()
}

func TestManualKeyboardInterruptsMacroWithoutClearingManualKeys(t *testing.T) {
	k, _ := configureTestHID(t)
	s := &Session{}
	done := make(chan error, 1)
	go func() { done <- s.executeKeyboardMacro(macroStep(4)) }()
	deadline := time.Now().Add(time.Second)
	for {
		b, _ := os.ReadFile(k)
		if len(b) == 8 && b[2] == 4 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("macro never wrote")
		}
		time.Sleep(time.Millisecond)
	}
	if e := s.keyboardReport(2, []byte{5}); e != nil {
		t.Fatal(e)
	}
	select {
	case e := <-done:
		if !errors.Is(e, context.Canceled) {
			t.Errorf("macro result=%v", e)
		}
	case <-time.After(200 * time.Millisecond):
		t.Error("manual input did not interrupt macro")
		keyboardMacros.cancelOwned(s)
		<-done
	}
	checkInputBytes(t, k, []byte{2, 0, 5, 0, 0, 0, 0, 0})
	s.closeHIDInput()
}

func TestSessionMacroInitialClearAndMouseIndependence(t *testing.T) {
	k, _ := configureTestHID(t)
	s := &Session{}
	if e := s.keyboardReport(2, []byte{5}); e != nil {
		t.Fatal(e)
	}
	steps := macroStep(0)
	steps[0].Delay = 0
	if e := s.executeKeyboardMacro(steps); e != nil {
		t.Fatal(e)
	}
	checkInputBytes(t, k, make([]byte, 8))
	done := make(chan error, 1)
	go func() { done <- s.executeKeyboardMacro(macroStep(4)) }()
	deadline := time.Now().Add(time.Second)
	for {
		b, _ := os.ReadFile(k)
		if len(b) == 8 && b[2] == 4 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("macro never wrote")
		}
		time.Sleep(time.Millisecond)
	}
	if e := s.withHID(true, func() error { return rpcRelMouseReport(1, 0, 0) }); e != nil {
		t.Fatal(e)
	}
	select {
	case e := <-done:
		t.Fatalf("mouse canceled keyboard macro: %v", e)
	case <-time.After(20 * time.Millisecond):
	}
	keyboardMacros.cancelOwned(s)
	if e := awaitMacro(t, done); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	checkInputBytes(t, k, make([]byte, 8))
	s.closeHIDInput()
}
