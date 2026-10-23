package kvm

import "sync/atomic"

var (
	currentSession atomic.Pointer[Session]
	actionSessions atomic.Int32
)

func loadCurrentSession() *Session {
	return currentSession.Load()
}

func replaceCurrentSession(session *Session) *Session {
	previous := currentSession.Swap(session)
	if previous != nil && previous != session {
		previous.stopHIDQueues()
	}
	return previous
}

func clearCurrentSession(session *Session) {
	currentSession.CompareAndSwap(session, nil)
}

func activeSessionCount() int32 {
	return actionSessions.Load()
}
