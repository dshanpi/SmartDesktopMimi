package hidrpc

import (
	"fmt"

	"kvm/internal/usbgadget"
)

type MessageType byte

const (
	TypeHandshake                 MessageType = 0x01
	TypeKeyboardReport            MessageType = 0x02
	TypePointerReport             MessageType = 0x03
	TypeWheelReport               MessageType = 0x04
	TypeKeypressReport            MessageType = 0x05
	TypeKeypressKeepAliveReport   MessageType = 0x09
	TypeMouseReport               MessageType = 0x06
	TypeKeyboardMacroReport       MessageType = 0x07
	TypeCancelKeyboardMacroReport MessageType = 0x08
	TypeKeyboardLedState          MessageType = 0x32
	TypeKeydownState              MessageType = 0x33
	TypeKeyboardMacroState        MessageType = 0x34
)

const (
	Version byte = 0x01
)

func GetQueueIndex(messageType MessageType) int {
	switch messageType {
	case TypeHandshake:
		return 0
	case TypeKeyboardReport, TypeKeypressReport, TypeKeyboardMacroReport, TypeKeyboardLedState, TypeKeydownState, TypeKeyboardMacroState:
		return 1
	case TypePointerReport, TypeMouseReport, TypeWheelReport:
		return 2
	case TypeCancelKeyboardMacroReport:
		return 3
	default:
		return 3
	}
}

func Unmarshal(data []byte, message *Message) error {
	l := len(data)
	if l < 1 {
		return fmt.Errorf("invalid data length: %d", l)
	}

	message.t = MessageType(data[0])
	message.d = data[1:]
	return nil
}

func Marshal(message *Message) ([]byte, error) {
	if message.t == 0 {
		return nil, fmt.Errorf("invalid message type: %d", message.t)
	}

	data := make([]byte, len(message.d)+1)
	data[0] = byte(message.t)
	copy(data[1:], message.d)

	return data, nil
}

func NewHandshakeMessage() *Message {
	return &Message{
		t: TypeHandshake,
		d: []byte{Version},
	}
}

func NewKeyboardReportMessage(keys []byte, modifier uint8) *Message {
	return &Message{
		t: TypeKeyboardReport,
		d: append([]byte{modifier}, keys...),
	}
}

func NewKeyboardLedMessage(state usbgadget.KeyboardState) *Message {
	return &Message{
		t: TypeKeyboardLedState,
		d: []byte{state.Byte()},
	}
}

func NewKeydownStateMessage(state usbgadget.KeysDownState) *Message {
	data := make([]byte, len(state.Keys)+1)
	data[0] = state.Modifier
	copy(data[1:], state.Keys)

	return &Message{
		t: TypeKeydownState,
		d: data,
	}
}

func NewKeyboardMacroStateMessage(state bool, isPaste bool) *Message {
	data := make([]byte, 2)
	if state {
		data[0] = 1
	}
	if isPaste {
		data[1] = 1
	}

	return &Message{
		t: TypeKeyboardMacroState,
		d: data,
	}
}

