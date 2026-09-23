package kvm

import (
	"sync"

	"github.com/google/uuid"
)

type VideoBroadcaster struct {
	subscribers       map[string]chan []byte
	lock              sync.RWMutex
	onFirstSubscribe  func()
	onLastUnsubscribe func()
}

var videoBroadcaster = &VideoBroadcaster{
	subscribers: make(map[string]chan []byte),
}

func (b *VideoBroadcaster) Subscribe() (string, chan []byte) {
	b.lock.Lock()
	id := uuid.New().String()
	// Buffer a bit to avoid dropping frames too easily,
	// but not too much to avoid latency build-up
	ch := make(chan []byte, 200)
	wasEmpty := len(b.subscribers) == 0
	b.subscribers[id] = ch
	callback := b.onFirstSubscribe
	b.lock.Unlock()
	if wasEmpty && callback != nil {
		callback()
	}
	return id, ch
}

func (b *VideoBroadcaster) Unsubscribe(id string) {
	b.lock.Lock()
	var callback func()
	if ch, ok := b.subscribers[id]; ok {
		close(ch)
		delete(b.subscribers, id)
		if len(b.subscribers) == 0 {
			callback = b.onLastUnsubscribe
		}
	}
	b.lock.Unlock()
	if callback != nil {
		callback()
	}
}

func (b *VideoBroadcaster) Broadcast(data []byte) {
	b.lock.RLock()
	defer b.lock.RUnlock()
	for _, ch := range b.subscribers {
		// Non-blocking send
		select {
		case ch <- data:
		default:
			// Drop frame if channel is full to avoid blocking other subscribers
			// Ideally we should have a ring buffer or similar, but this is simple
		}
	}
}
