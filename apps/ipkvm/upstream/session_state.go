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
	return currentSession.Swap(session)
}

func clearCurrentSession(session *Session) {
	currentSession.CompareAndSwap(session, nil)
}

func activeSessionCount() int32 {
	return actionSessions.Load()
}
