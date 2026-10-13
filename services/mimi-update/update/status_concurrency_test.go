package update

import (
	"context"
	"net/http/httptest"
	"testing"
	"time"
)

func TestSlowStatusReadDoesNotBlockCancellation(t *testing.T) {
	e, _, _, _, _, _ := engineFixture(t)
	trust := e.Trust
	entered, release, done := make(chan struct{}), make(chan struct{}), make(chan struct{})
	e.Trust = func() (Trust, error) {
		close(entered)
		<-release
		return trust()
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s := &Service{Engine: e, busy: true, phase: "download", cancel: cancel}
	handler := s.Handler()
	go func() {
		defer close(done)
		handler.ServeHTTP(httptest.NewRecorder(), httptest.NewRequest("GET", "/status", nil))
	}()
	defer func() { close(release); <-done }()
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("status did not reach the simulated slow storage read")
	}
	result := make(chan int, 1)
	go func() {
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, httptest.NewRequest("POST", "/cancel", nil))
		result <- response.Code
	}()
	select {
	case code := <-result:
		if code != 202 || ctx.Err() != context.Canceled {
			t.Fatalf("cancel did not reach the active download: status=%d error=%v", code, ctx.Err())
		}
	case <-time.After(time.Second):
		t.Fatal("slow status read blocked cancellation")
	}
}
