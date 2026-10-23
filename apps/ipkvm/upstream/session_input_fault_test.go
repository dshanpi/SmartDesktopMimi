package kvm

import (
	"os"
	"testing"
)

func TestSessionReleaseFaultRetainsOnlyFailedOwner(t *testing.T) {
	for _, failedMouse := range []bool{false, true} {
		t.Run(map[bool]string{false: "keyboard", true: "mouse"}[failedMouse], func(t *testing.T) {
			k, m := configureTestHID(t)
			s := &Session{}
			if e := s.keyboardReport(2, []byte{4}); e != nil {
				t.Fatal(e)
			}
			if e := s.withHID(true, func() error { return rpcRelMouseReport(1, 0, 1) }); e != nil {
				t.Fatal(e)
			}
			failed := k
			if failedMouse {
				failed = m
			}
			held, e := os.ReadFile(failed)
			if e != nil {
				t.Fatal(e)
			}
			if e = os.Remove(failed); e != nil {
				t.Fatal(e)
			}
			s.closeHIDInput()
			sessionInput.Lock()
			ko, mo := sessionInput.keyboard, sessionInput.mouse
			sessionInput.Unlock()
			if failedMouse {
				if ko != nil || mo != s {
					t.Fatalf("owners keyboard=%p mouse=%p", ko, mo)
				}
				checkInputBytes(t, k, make([]byte, 8))
			} else {
				if ko != s || mo != nil {
					t.Fatalf("owners keyboard=%p mouse=%p", ko, mo)
				}
				checkInputBytes(t, m, make([]byte, 4))
			}
			if e = os.WriteFile(failed, held, 0600); e != nil {
				t.Fatal(e)
			}
			s.closeHIDInput()
			checkInputBytes(t, k, make([]byte, 8))
			checkInputBytes(t, m, make([]byte, 4))
		})
	}
}
func TestFailedReleaseRetryDoesNotClearNewOwner(t *testing.T) {
	k, _ := configureTestHID(t)
	a, b := &Session{}, &Session{}
	if e := a.keyboardReport(2, []byte{4}); e != nil {
		t.Fatal(e)
	}
	if e := os.Remove(k); e != nil {
		t.Fatal(e)
	}
	a.closeHIDInput()
	if e := os.WriteFile(k, make([]byte, 8), 0600); e != nil {
		t.Fatal(e)
	}
	if e := b.keyboardReport(1, []byte{5}); e != nil {
		t.Fatal(e)
	}
	a.closeHIDInput()
	checkInputBytes(t, k, []byte{1, 0, 5, 0, 0, 0, 0, 0})
	b.closeHIDInput()
}
func TestFailedSessionWriteStillGetsCloseCleanup(t *testing.T) {
	k, _ := configureTestHID(t)
	s := &Session{}
	if e := os.Remove(k); e != nil {
		t.Fatal(e)
	}
	if e := s.keyboardReport(2, []byte{4}); e == nil {
		t.Fatal("missing endpoint write succeeded")
	}
	// A failed write may have partially reached real hardware; retain ownership.
	if e := os.WriteFile(k, []byte{2, 0, 4, 0, 0, 0, 0, 0}, 0600); e != nil {
		t.Fatal(e)
	}
	s.closeHIDInput()
	checkInputBytes(t, k, make([]byte, 8))
}
