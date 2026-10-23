package kvm

import (
	"bytes"
	"kvm/internal/hidrpc"
	"os"
	"testing"
)

func TestHIDDispatcherRejectsTruncatedPayload(t *testing.T) {
	keyboard, mouse := configureTestHID(t)
	for _, c := range []struct {
		typ     hidrpc.MessageType
		minimum int
	}{
		{hidrpc.TypeKeypressReport, 2}, {hidrpc.TypeKeyboardReport, 1},
		{hidrpc.TypeKeyboardMacroReport, 5}, {hidrpc.TypePointerReport, 9},
		{hidrpc.TypeMouseReport, 3}, {hidrpc.TypeWheelReport, 1},
	} {
		for n := 0; n < c.minimum; n++ {
			var msg hidrpc.Message
			if e := hidrpc.Unmarshal(append([]byte{byte(c.typ)}, make([]byte, n)...), &msg); e != nil {
				t.Fatal(e)
			}
			handleHidRPCMessage(msg, &Session{})
		}
	}
	for path, size := range map[string]int{keyboard: 8, mouse: 4} {
		b, e := os.ReadFile(path)
		if e != nil {
			t.Fatal(e)
		}
		if !bytes.Equal(b, make([]byte, size)) {
			t.Fatalf("malformed input wrote HID report: %v", b)
		}
	}
}
