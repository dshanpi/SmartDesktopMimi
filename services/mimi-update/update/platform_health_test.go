package update

import "testing"

func TestUIHealthRequiresCompleteActivityReplies(t *testing.T) {
	base := UIHealth{PID: 42, Tick: 9000, Backend: 8500, Ready: true}
	cases := []struct {
		name      string
		mutate    func(*UIHealth)
		wantError bool
	}{
		{"all replies fresh", func(h *UIHealth) {}, false},
		{"active operation is healthy but not idle", func(h *UIHealth) { h.Busy = true }, false},
		{"legacy backend answers wifi but omits video status", func(h *UIHealth) { h.Ready = false }, true},
		{"event loop stalled", func(h *UIHealth) { h.Tick = 5999 }, true},
		{"backend stalled", func(h *UIHealth) { h.Backend = 4999 }, true},
		{"future event loop receipt", func(h *UIHealth) { h.Tick = 10001 }, true},
		{"future backend receipt", func(h *UIHealth) { h.Backend = 10001 }, true},
		{"missing process", func(h *UIHealth) { h.PID = 0 }, true},
	}
	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			h := base
			tt.mutate(&h)
			err := validateUIHealth(h, 10000)
			if (err != nil) != tt.wantError {
				t.Fatalf("health=%+v err=%v", h, err)
			}
		})
	}
}
