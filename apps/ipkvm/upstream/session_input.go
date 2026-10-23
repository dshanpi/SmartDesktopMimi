package kvm

import (
	"context"
	"errors"
	"kvm/internal/hidrpc"
	"sync"
)

type keyboardInputToken struct{ marker byte }

var sessionInput = struct {
	sync.Mutex
	keyboard           *Session
	keyboardToken      *keyboardInputToken
	keyboardGeneration uint64
	mouse              *Session
}{}

// All session input writes and close/release share this lock. A report already
// inside the device write finishes before close sends the release report.
func (s *Session) withHID(mouse bool, operation func() error) error {
	sessionInput.Lock()
	defer sessionInput.Unlock()
	if s.inputClosed {
		return context.Canceled
	}
	if mouse {
		sessionInput.mouse = s
	} else {
		// Manual keyboard input supersedes any older macro, including its
		// deferred key-release report. Mouse movement does not interrupt it.
		sessionInput.keyboardGeneration++
		keyboardMacros.cancel()
		sessionInput.keyboardToken = nil
		sessionInput.keyboard = s
	}
	return operation()
}
func (s *Session) keyboardReport(mod uint8, keys []uint8) error {
	return s.withHID(false, func() error { return rpcKeyboardReport(mod, keys) })
}
func (s *Session) closeHIDInput() {
	sessionInput.Lock()
	defer sessionInput.Unlock()
	s.inputClosed = true
	if s.inputCancel != nil {
		s.inputCancel()
	}
	var err error
	if sessionInput.keyboard == s {
		e := rpcKeyboardReport(0, make([]uint8, 6))
		err = errors.Join(err, e)
		if e == nil {
			sessionInput.keyboard = nil
		}
	}
	if sessionInput.mouse == s {
		e := rpcRelMouseReport(0, 0, 0)
		err = errors.Join(err, e)
		if e == nil {
			sessionInput.mouse = nil
		}
	}
	if err != nil {
		hidRPCLogger.Warn().Err(err).Msg("failed to release closed session input")
	}
}
func (s *Session) executeKeyboardMacro(steps []hidrpc.KeyboardMacroStep) error {
	sessionInput.Lock()
	if s.inputClosed {
		sessionInput.Unlock()
		return context.Canceled
	}
	if s.inputContext == nil {
		s.inputContext, s.inputCancel = context.WithCancel(context.Background())
	}
	ctx := s.inputContext
	generation := sessionInput.keyboardGeneration
	token := &keyboardInputToken{}
	started := false
	sessionInput.Unlock()
	return keyboardMacros.executeOwned(ctx, s, steps, func(mod uint8, keys []uint8) error {
		sessionInput.Lock()
		defer sessionInput.Unlock()
		if s.inputClosed || generation != sessionInput.keyboardGeneration {
			return context.Canceled
		}
		clear := mod == 0
		for _, key := range keys {
			if key != 0 {
				clear = false
			}
		}
		if clear && started && (sessionInput.keyboard != s || sessionInput.keyboardToken != token) {
			return nil
		}
		sessionInput.keyboard = s
		sessionInput.keyboardToken = token
		started = true
		return rpcKeyboardReport(mod, keys)
	})
}
