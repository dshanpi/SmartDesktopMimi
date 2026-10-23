package kvm

import (
	"context"
	"errors"
	"kvm/internal/hidrpc"
	"sync"
	"time"
)

type keyboardMacroRun struct {
	ctx    context.Context
	cancel context.CancelFunc
}
type keyboardMacroController struct {
	mu      sync.Mutex
	output  sync.Mutex
	current *keyboardMacroRun
}

var keyboardMacros keyboardMacroController

func (c *keyboardMacroController) cancel() {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.current != nil {
		c.current.cancel()
	}
}
func (c *keyboardMacroController) execute(steps []hidrpc.KeyboardMacroStep, report func(uint8, []uint8) error) error {
	ctx, cancel := context.WithCancel(context.Background())
	run := &keyboardMacroRun{ctx: ctx, cancel: cancel}
	c.mu.Lock()
	if c.current != nil {
		c.current.cancel()
	}
	c.current = run
	c.mu.Unlock()
	defer func() {
		cancel()
		c.mu.Lock()
		if c.current == run {
			c.current = nil
		}
		c.mu.Unlock()
	}()
	// A replacement cannot emit until the previous macro has released its keys.
	// A newer request may cancel a macro while it waits for this lock.
	c.output.Lock()
	defer c.output.Unlock()
	return executeKeyboardMacroSteps(ctx, steps, report)
}
func rpcExecuteKeyboardMacro(steps []hidrpc.KeyboardMacroStep) error {
	return keyboardMacros.execute(steps, rpcKeyboardReport)
}
func rpcCancelKeyboardMacro() { keyboardMacros.cancel() }

func executeKeyboardMacroSteps(ctx context.Context, steps []hidrpc.KeyboardMacroStep, report func(uint8, []uint8) error) (err error) {
	attempted := false
	defer func() {
		if attempted {
			err = errors.Join(err, report(0, make([]uint8, 6)))
		}
	}()
	for _, step := range steps {
		if err = ctx.Err(); err != nil {
			return err
		}
		keys := make([]uint8, len(step.Keys))
		for i, k := range step.Keys {
			keys[i] = uint8(k)
		}
		attempted = true
		if err = report(uint8(step.Modifier), keys); err != nil {
			return err
		}
		timer := time.NewTimer(time.Duration(step.Delay) * time.Millisecond)
		select {
		case <-ctx.Done():
			timer.Stop()
			return ctx.Err()
		case <-timer.C:
		}
	}
	return ctx.Err()
}
