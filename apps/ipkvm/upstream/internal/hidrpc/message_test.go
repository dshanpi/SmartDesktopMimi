package hidrpc

import (
	"encoding/binary"
	"fmt"
	"testing"
)

func decodeForTest(m *Message) error {
	switch m.Type() {
	case TypeKeypressReport:
		_, e := m.KeypressReport()
		return e
	case TypeKeyboardReport:
		_, e := m.KeyboardReport()
		return e
	case TypeKeyboardMacroReport:
		_, e := m.KeyboardMacroReport()
		return e
	case TypePointerReport:
		_, e := m.PointerReport()
		return e
	case TypeMouseReport:
		_, e := m.MouseReport()
		return e
	case TypeWheelReport:
		_, e := m.WheelReport()
		return e
	case TypeKeyboardMacroState:
		_, e := m.KeyboardMacroState()
		return e
	}
	return fmt.Errorf("unknown")
}
func TestPayloadBounds(t *testing.T) {
	cases := []struct {
		typ     MessageType
		valid   []byte
		minimum int
	}{
		{TypeKeypressReport, []byte{4, 1}, 2}, {TypeKeyboardReport, []byte{2, 4, 5, 0, 0, 0, 0}, 1},
		{TypeKeyboardMacroReport, []byte{1, 0, 0, 0, 1, 2, 4, 0, 0, 0, 0, 0, 0, 10}, 14},
		{TypePointerReport, make([]byte, 9), 9}, {TypeMouseReport, []byte{1, 2, 1}, 3},
		{TypeWheelReport, []byte{255}, 1}, {TypeKeyboardMacroState, []byte{1, 0}, 2},
	}
	for _, c := range cases {
		for n := 0; n < c.minimum; n++ {
			t.Run(fmt.Sprintf("type%d-short%d", c.typ, n), func(t *testing.T) {
				defer func() {
					if p := recover(); p != nil {
						t.Errorf("payload panic: %v", p)
					}
				}()
				var m Message
				if err := Unmarshal(append([]byte{byte(c.typ)}, c.valid[:n]...), &m); err != nil {
					t.Fatal(err)
				}
				if err := decodeForTest(&m); err == nil {
					t.Error("truncated payload accepted")
				}
			})
		}
		t.Run(fmt.Sprintf("type%d-valid", c.typ), func(t *testing.T) {
			m := Message{t: c.typ, d: c.valid}
			if e := decodeForTest(&m); e != nil {
				t.Fatal(e)
			}
		})
		t.Run(fmt.Sprintf("type%d-extra", c.typ), func(t *testing.T) {
			m := Message{t: c.typ, d: append(append([]byte{}, c.valid...), 0)}
			if e := decodeForTest(&m); e == nil {
				t.Error("extra payload accepted")
			}
		})
	}
}
func TestMacroCountAndKeyboardCompatibility(t *testing.T) {
	for n := 0; n <= 6; n++ {
		m := Message{t: TypeKeyboardReport, d: make([]byte, n+1)}
		if _, e := m.KeyboardReport(); e != nil {
			t.Fatalf("%d keys: %v", n, e)
		}
	}
	for _, count := range []uint32{1, 0x1c71c71c, 0xffffffff} {
		d := make([]byte, 5)
		binary.BigEndian.PutUint32(d[1:], count)
		m := Message{t: TypeKeyboardMacroReport, d: d}
		if _, e := m.KeyboardMacroReport(); e == nil {
			t.Fatalf("count%d accepted empty body", count)
		}
	}
	m := Message{t: TypeKeyboardMacroReport, d: make([]byte, 5)}
	v, e := m.KeyboardMacroReport()
	if e != nil || len(v.Steps) != 0 {
		t.Fatalf("empty macro=%v %v", v, e)
	}
}
