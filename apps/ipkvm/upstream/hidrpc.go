package kvm

import (
	"context"
	"sync"
	"time"

	"kvm/internal/hidrpc"
)

type keypressState struct {
	lock     sync.Mutex
	modifier uint8
	keys     [6]uint8
}

func (s *keypressState) applyModifierKey(key uint8, press bool) {
	if key < 0xE0 || key > 0xE7 {
		return
	}
	mask := uint8(1) << (key - 0xE0)
	if press {
		s.modifier |= mask
	} else {
		s.modifier &^= mask
	}
}

func (s *keypressState) pressKey(key uint8) {
	for i := range s.keys {
		if s.keys[i] == key {
			return
		}
	}
	for i := range s.keys {
		if s.keys[i] == 0 {
			s.keys[i] = key
			return
		}
	}
}

func (s *keypressState) releaseKey(key uint8) {
	for i := range s.keys {
		if s.keys[i] == key {
			s.keys[i] = 0
		}
	}
}

func (s *keypressState) handleKeypress(key uint8, press bool) error {
	s.lock.Lock()
	defer s.lock.Unlock()

	s.applyModifierKey(key, press)

	if key < 0xE0 || key > 0xE7 {
		if press {
			s.pressKey(key)
		} else {
			s.releaseKey(key)
		}
	}

	keys := make([]uint8, 6)
	copy(keys, s.keys[:])
	return rpcKeyboardReport(s.modifier, keys)
}

var (
	keyboardMacroCancel context.CancelFunc
	keyboardMacroLock   sync.Mutex
)

func cancelKeyboardMacro() {
	keyboardMacroLock.Lock()
	defer keyboardMacroLock.Unlock()

	if keyboardMacroCancel != nil {
		keyboardMacroCancel()
		keyboardMacroCancel = nil
	}
}

func setKeyboardMacroCancel(cancel context.CancelFunc) {
	keyboardMacroLock.Lock()
	defer keyboardMacroLock.Unlock()
	keyboardMacroCancel = cancel
}

func rpcExecuteKeyboardMacro(macro []hidrpc.KeyboardMacroStep) error {
	cancelKeyboardMacro()

	ctx, cancel := context.WithCancel(context.Background())
	setKeyboardMacroCancel(cancel)

	err := rpcDoExecuteKeyboardMacro(ctx, macro)
	setKeyboardMacroCancel(nil)
	return err
}

func rpcCancelKeyboardMacro() {
	cancelKeyboardMacro()
}

var keyboardClearStateKeys = make([]uint8, 6)

func isClearKeyStep(step hidrpc.KeyboardMacroStep) bool {
	if step.Modifier != 0 {
		return false
	}
	if len(step.Keys) != 6 {
		return false
	}
	for i := 0; i < 6; i++ {
		if step.Keys[i] != 0 {
			return false
		}
	}
	return true
}

func rpcDoExecuteKeyboardMacro(ctx context.Context, macro []hidrpc.KeyboardMacroStep) error {
	for _, step := range macro {
		delay := time.Duration(step.Delay) * time.Millisecond

		keys := make([]uint8, 0, len(step.Keys))
		for _, b := range step.Keys {
			keys = append(keys, uint8(b))
		}

		if err := rpcKeyboardReport(uint8(step.Modifier), keys); err != nil {
			return err
		}

		select {
		case <-time.After(delay):
		case <-ctx.Done():
			_ = rpcKeyboardReport(0, keyboardClearStateKeys)
			return ctx.Err()
		}

		if isClearKeyStep(step) {
			_ = rpcKeyboardReport(0, keyboardClearStateKeys)
		}
	}

	return nil
}

func handleHidRPCMessage(message hidrpc.Message, session *Session) {
	var rpcErr error

	switch message.Type() {
	case hidrpc.TypeHandshake:
		handshake, err := hidrpc.NewHandshakeMessage().Marshal()
		if err != nil {
			hidRPCLogger.Warn().Err(err).Msg("failed to marshal handshake message")
			return
		}
		if session.HidChannel != nil {
			if err := session.HidChannel.Send(handshake); err != nil {
				hidRPCLogger.Warn().Err(err).Msg("failed to send handshake message")
				return
			}
		}
		session.hidRPCAvailable = true
	case hidrpc.TypeKeypressReport:
		keypressReport, err := message.KeypressReport()
		if err != nil {
			hidRPCLogger.Warn().Err(err).Msg("failed to get keypress report")
			return
		}
		if session.keypress == nil {
			session.keypress = &keypressState{}
		}
		rpcErr = session.keypress.handleKeypress(uint8(keypressReport.Key), keypressReport.Press)
	case hidrpc.TypeKeyboardReport:
		keyboardReport, err := message.KeyboardReport()
		if err != nil {
			hidRPCLogger.Warn().Err(err).Msg("failed to get keyboard report")
			return
		}
		keys := make([]uint8, len(keyboardReport.Keys))
		for i, k := range keyboardReport.Keys {
			keys[i] = uint8(k)
		}
		rpcErr = rpcKeyboardReport(uint8(keyboardReport.Modifier), keys)
	case hidrpc.TypeKeyboardMacroReport:
		keyboardMacroReport, err := message.KeyboardMacroReport()
		if err != nil {
			hidRPCLogger.Warn().Err(err).Msg("failed to get keyboard macro report")
			return
		}
		rpcErr = rpcExecuteKeyboardMacro(keyboardMacroReport.Steps)
	case hidrpc.TypeCancelKeyboardMacroReport:
		rpcCancelKeyboardMacro()
		return
	case hidrpc.TypeKeypressKeepAliveReport:
		return
	case hidrpc.TypePointerReport:
		pointerReport, err := message.PointerReport()
		if err != nil {
			hidRPCLogger.Warn().Err(err).Msg("failed to get pointer report")
			return
		}
		rpcErr = rpcAbsMouseReport(pointerReport.X, pointerReport.Y, pointerReport.Button)
	case hidrpc.TypeMouseReport:
		mouseReport, err := message.MouseReport()
		if err != nil {
			hidRPCLogger.Warn().Err(err).Msg("failed to get mouse report")
			return
		}
		rpcErr = rpcRelMouseReport(mouseReport.DX, mouseReport.DY, mouseReport.Button)
	case hidrpc.TypeWheelReport:
		wheelReport, err := message.WheelReport()
		if err != nil {
			hidRPCLogger.Warn().Err(err).Msg("failed to get wheel report")
			return
		}
		rpcErr = rpcWheelReport(wheelReport.WheelY)
	default:
		hidRPCLogger.Warn().Uint8("type", uint8(message.Type())).Msg("unknown HID RPC message type")
		return
	}

	if rpcErr != nil {
		hidRPCLogger.Warn().Err(rpcErr).Msg("failed to handle HID RPC message")
	}
}

func onHidMessage(msg hidQueueMessage, session *Session) {
	data := msg.Data

	scopedLogger := hidRPCLogger.With().Str("channel", msg.channel).Logger()
	if len(data) < 1 {
		scopedLogger.Warn().Int("length", len(data)).Msg("received empty data in HID RPC message handler")
		return
	}

	var message hidrpc.Message
	if err := hidrpc.Unmarshal(data, &message); err != nil {
		scopedLogger.Warn().Err(err).Msg("failed to unmarshal HID RPC message")
		return
	}

	t := time.Now()
	r := make(chan struct{})
	go func() {
		handleHidRPCMessage(message, session)
		close(r)
	}()
	select {
	case <-time.After(1 * time.Second):
		scopedLogger.Warn().Msg("HID RPC message timed out")
	case <-r:
		scopedLogger.Debug().Dur("duration", time.Since(t)).Msg("HID RPC message handled")
	}
}
