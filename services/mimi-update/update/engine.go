package update

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

type State struct {
	Schema    int      `json:"schema_version"`
	Current   string   `json:"current"`
	Previous  string   `json:"previous"`
	Trial     bool     `json:"trial"`
	HighWater uint64   `json:"high_water"`
	Failed    []string `json:"failed"`
	History   []Event  `json:"history"`
}
type Event struct {
	Time    string `json:"time"`
	Version string `json:"version"`
	Result  string `json:"result"`
	Reason  string `json:"reason"`
}
type Hooks interface {
	Preflight() error
	Device() (Device, error)
	Idle() error
	Stop() error
	Start(string) error
	Healthy(string, time.Duration) error
}
type Engine struct {
	Root    string
	Factory string
	Lock    string
	Trust   func() (Trust, error)
	Hooks   Hooks
	// Fault is only supplied by host tests; it is not a device/configuration option.
	Fault func(string) error
}

func (e *Engine) lock() (func(), error) {
	f, err := os.OpenFile(e.Lock, os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		return nil, err
	}
	if err = syscall.Flock(int(f.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		f.Close()
		return nil, fmt.Errorf("another application/system update is active")
	}
	return func() { syscall.Flock(int(f.Fd()), syscall.LOCK_UN); f.Close() }, nil
}
func (e *Engine) readState() (State, error) {
	s := State{Schema: 1, Current: "factory"}
	raw, err := os.ReadFile(filepath.Join(e.Root, "state.json"))
	if errors.Is(err, os.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return s, err
	}
	if err = StrictJSON(raw, &s); err != nil {
		return s, err
	}
	if s.Schema != 1 || !validID(s.Current) || (s.Previous != "" && !validID(s.Previous)) {
		return s, fmt.Errorf("invalid update journal")
	}
	return s, nil
}
func validID(id string) bool { return id == "factory" || sha256Pattern.MatchString(id) }
func (e *Engine) dir(id string) string {
	if id == "factory" {
		return e.Factory
	}
	return filepath.Join(e.Root, "releases", id)
}
func (e *Engine) writeState(s State) error {
	if err := os.MkdirAll(e.Root, 0700); err != nil {
		return err
	}
	raw, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	f, err := os.CreateTemp(e.Root, ".state-")
	if err != nil {
		return err
	}
	tmp := f.Name()
	defer os.Remove(tmp)
	if _, err = f.Write(raw); err == nil {
		err = f.Sync()
	}
	ce := f.Close()
	if err != nil {
		return err
	}
	if ce != nil {
		return ce
	}
	if err = os.Rename(tmp, filepath.Join(e.Root, "state.json")); err != nil {
		return err
	}
	return syncDir(e.Root)
}
func (e *Engine) point(p string) error {
	if e.Fault != nil {
		return e.Fault(p)
	}
	return nil
}
func (e *Engine) installed(id string, d Device) (Manifest, error) {
	var m Manifest
	if !validID(id) {
		return m, fmt.Errorf("invalid release id")
	}
	dir := e.dir(id)
	info, err := os.Lstat(dir)
	if err != nil {
		return m, err
	}
	if !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
		return m, fmt.Errorf("invalid release directory")
	}
	raw, err := os.ReadFile(filepath.Join(dir, "manifest.json"))
	if err != nil {
		return m, err
	}
	sig, err := os.ReadFile(filepath.Join(dir, "manifest.sig"))
	if err != nil {
		return m, err
	}
	trust, err := e.Trust()
	if err != nil {
		return m, err
	}
	m, err = Verify(raw, sig, trust)
	if err != nil {
		return m, err
	}
	if id != "factory" && m.PackageSHA256 != id {
		return m, fmt.Errorf("release identity mismatch")
	}
	if err = Match(m, d); err != nil {
		return m, err
	}
	return m, VerifyTree(dir, m)
}
func event(s *State, m Manifest, result, reason string) {
	s.History = append(s.History, Event{Time: time.Now().UTC().Format(time.RFC3339), Version: m.Version, Result: result, Reason: reason})
	if len(s.History) > 100 {
		s.History = s.History[len(s.History)-100:]
	}
}
func (e *Engine) rollback(s *State, d Device, reason string) error {
	failed := s.Current
	for _, id := range []string{s.Previous, "factory"} {
		if id == "" || id == failed {
			continue
		}
		m, err := e.installed(id, d)
		if err != nil {
			continue
		}
		s.Current = id
		s.Previous = ""
		s.Trial = false
		s.Failed = append(s.Failed, failed)
		if len(s.Failed) > 100 {
			s.Failed = s.Failed[len(s.Failed)-100:]
		}
		event(s, m, "rolled_back", reason)
		return e.writeState(*s)
	}
	return fmt.Errorf("no trusted compatible rollback version: %s", reason)
}

// Select is used before EVERY UI start, including system A/B rollback. It verifies
// the persisted application against the currently booted system, not a cached one.
func (e *Engine) Select() (string, error) {
	unlock, err := e.lock()
	if err != nil {
		return "", err
	}
	defer unlock()
	return e.selectLocked()
}

// StartSelected keeps admission and activation in the same transaction. A
// concurrent application or system update must not replace the selected tree
// between its verification and the platform launch.
func (e *Engine) StartSelected() error {
	unlock, err := e.lock()
	if err != nil {
		return err
	}
	defer unlock()
	dir, err := e.selectLocked()
	if err != nil {
		return err
	}
	return e.Hooks.Start(dir)
}
func (e *Engine) selectLocked() (string, error) {
	s, err := e.readState()
	if err != nil {
		return "", err
	}
	d, err := e.Hooks.Device()
	if err != nil {
		return "", err
	}
	if _, err = e.reconcileSystem(d); err != nil {
		return "", err
	}
	if s.Trial {
		if err = e.rollback(&s, d, "interrupted trial recovered before UI start"); err != nil {
			return "", err
		}
	}
	if _, err = e.installed(s.Current, d); err != nil {
		if err = e.rollback(&s, d, err.Error()); err != nil {
			return "", err
		}
	}
	e.collect(s)
	return e.dir(s.Current), nil
}
func (e *Engine) Status() (State, error) { return e.readState() }
func (e *Engine) Install(archive string, raw, sig []byte, channel string) error {
	unlock, err := e.lock()
	if err != nil {
		return err
	}
	defer unlock()
	if err = e.Hooks.Preflight(); err != nil {
		return err
	}
	if _, err = e.selectLocked(); err != nil {
		return err
	}
	system, err := systemJournal(filepath.Join(e.Root, "system"))
	if err != nil {
		return err
	}
	if system.Phase == "applying" || system.Phase == "pending-reboot" {
		return fmt.Errorf("pending system transaction blocks application install")
	}
	s, err := e.readState()
	if err != nil {
		return err
	}
	d, err := e.Hooks.Device()
	if err != nil {
		return err
	}
	trust, err := e.Trust()
	if err != nil {
		return err
	}
	m, err := Verify(raw, sig, trust)
	if err != nil {
		return err
	}
	if m.Channel != channel {
		return fmt.Errorf("release channel mismatch")
	}
	if err = Match(m, d); err != nil {
		return err
	}
	old, err := e.installed(s.Current, d)
	if err != nil {
		return err
	}
	cmp, err := Compare(m.Version, old.Version)
	if err != nil {
		return err
	}
	if cmp <= 0 || m.Sequence <= s.HighWater || m.Sequence <= old.Sequence {
		return fmt.Errorf("downgrade/replay rejected")
	}
	for _, id := range s.Failed {
		if id == m.PackageSHA256 {
			return fmt.Errorf("previously failed candidate rejected")
		}
	}
	if err = e.Hooks.Idle(); err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Join(e.Root, "releases"), 0700); err != nil {
		return err
	}
	var fs syscall.Statfs_t
	if err = syscall.Statfs(e.Root, &fs); err != nil {
		return err
	}
	needed := uint64(m.ExpandedBytes) + 32<<20
	if uint64(fs.Bavail)*uint64(fs.Bsize) < needed {
		return fmt.Errorf("insufficient staging space")
	}
	stage, err := os.MkdirTemp(filepath.Join(e.Root, "releases"), ".stage-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(stage)
	if err = CheckArchive(archive, m, stage); err != nil {
		return err
	}
	if err = e.point("extracted"); err != nil {
		return err
	}
	if err = durableFile(filepath.Join(stage, "manifest.json"), raw, 0644); err != nil {
		return err
	}
	if err = durableFile(filepath.Join(stage, "manifest.sig"), sig, 0644); err != nil {
		return err
	}
	if err = syncDir(stage); err != nil {
		return err
	}
	final := e.dir(m.PackageSHA256)
	if _, err = os.Lstat(final); err == nil {
		if _, err = e.installed(m.PackageSHA256, d); err != nil {
			return fmt.Errorf("existing candidate differs: %w", err)
		}
		stored, readErr := os.ReadFile(filepath.Join(final, "manifest.json"))
		if readErr != nil || !bytes.Equal(stored, raw) {
			return fmt.Errorf("immutable release already has different metadata")
		}

	} else if errors.Is(err, os.ErrNotExist) {
		if err = os.Rename(stage, final); err != nil {
			return err
		}
		if err = syncDir(filepath.Dir(final)); err != nil {
			return err
		}
	} else {
		return err
	}
	if err = e.point("staged"); err != nil {
		return err
	}
	// Re-read platform identity, trust and busy state while holding the shared lock.
	d, err = e.Hooks.Device()
	if err != nil {
		return err
	}
	if _, err = e.installed(m.PackageSHA256, d); err != nil {
		return err
	}
	if err = e.Hooks.Idle(); err != nil {
		return err
	}
	if err = e.Hooks.Preflight(); err != nil {
		return err
	}
	s.Previous = s.Current
	s.Current = m.PackageSHA256
	s.Trial = true
	s.HighWater = m.Sequence
	event(&s, m, "trial", "")
	if err = e.writeState(s); err != nil {
		return err
	}
	if err = e.point("journaled"); err != nil {
		return err
	}
	if err = e.Hooks.Stop(); err == nil {
		err = e.point("stopped")
	}
	if err == nil {
		err = e.Hooks.Start(final)
	}
	if err == nil {
		err = e.point("started")
	}
	if err == nil {
		err = e.Hooks.Healthy(final, 60*time.Second)
	}
	if err != nil {
		cause := err
		stopErr := e.Hooks.Stop()
		if stopErr != nil {
			return fmt.Errorf("trial failed (%v); unable to stop trial (%v), recovery pending", cause, stopErr)
		}
		if err = e.rollback(&s, d, cause.Error()); err != nil {
			return err
		}
		if err = e.Hooks.Start(e.dir(s.Current)); err != nil {
			return fmt.Errorf("rollback start failed: %w", err)
		}
		return fmt.Errorf("candidate rolled back: %w", cause)
	}
	if err = e.point("healthy"); err != nil {
		return err
	}
	if err = e.Hooks.Preflight(); err != nil {
		return err
	}
	s.Trial = false
	event(&s, m, "committed", "")
	if err = e.writeState(s); err != nil {
		return err
	}
	e.collect(s)
	return nil
}

// Root paths come only from system configuration; package metadata never chooses them.
func (e *Engine) ValidatePaths() error {
	for _, p := range []string{e.Root, e.Factory, e.Lock} {
		if !filepath.IsAbs(p) || filepath.Clean(p) != p || strings.Contains(p, "\x00") {
			return fmt.Errorf("invalid system update path")
		}
	}
	return nil
}

// SystemHealth waits for desktop startup without holding its launch lock, then
// revalidates the selected application and health under the A/B commit lock.
func (e *Engine) SystemHealth(commit func() error) error {
	if err := e.Hooks.Preflight(); err != nil {
		return err
	}
	observed, err := e.Select()
	if err != nil {
		return err
	}
	if err = e.Hooks.Healthy(observed, 60*time.Second); err != nil {
		return err
	}
	unlock, err := e.lock()
	if err != nil {
		return err
	}
	defer unlock()
	if err = e.Hooks.Preflight(); err != nil {
		return err
	}
	dir, err := e.selectLocked()
	if err != nil {
		return err
	}
	if dir != observed {
		return fmt.Errorf("selected application changed while waiting for system health")
	}
	if err = e.Hooks.Healthy(dir, 60*time.Second); err != nil {
		return err
	}
	if err = e.Hooks.Preflight(); err != nil {
		return err
	}
	if commit != nil {
		return commit()
	}
	return nil
}

// Clean only updater-owned staging and signed, unreferenced application trees.
// User data lives outside releases; current/previous/factory are never collected.
func (e *Engine) collect(s State) {
	entries, err := os.ReadDir(filepath.Join(e.Root, "releases"))
	if err != nil {
		return
	}
	trust, err := e.Trust()
	if err != nil {
		return
	}
	for _, entry := range entries {
		id := entry.Name()
		if !entry.IsDir() || id == s.Current || id == s.Previous {
			continue
		}
		dir := filepath.Join(e.Root, "releases", id)
		if strings.HasPrefix(id, ".stage-") {
			_ = os.RemoveAll(dir)
			continue
		}
		if !sha256Pattern.MatchString(id) {
			continue
		}
		raw, err := os.ReadFile(filepath.Join(dir, "manifest.json"))
		if err != nil {
			continue
		}
		sig, err := os.ReadFile(filepath.Join(dir, "manifest.sig"))
		if err != nil {
			continue
		}
		m, err := Verify(raw, sig, trust)
		if err != nil || m.PackageSHA256 != id {
			continue
		}
		_ = os.RemoveAll(dir)
	}
	_ = syncDir(filepath.Join(e.Root, "releases"))
}

func (e *Engine) CurrentMetadata() (Manifest, error) {
	var m Manifest
	s, err := e.readState()
	if err != nil {
		return m, err
	}
	raw, err := os.ReadFile(filepath.Join(e.dir(s.Current), "manifest.json"))
	if err != nil {
		return m, err
	}
	sig, err := os.ReadFile(filepath.Join(e.dir(s.Current), "manifest.sig"))
	if err != nil {
		return m, err
	}
	trust, err := e.Trust()
	if err != nil {
		return m, err
	}
	return Verify(raw, sig, trust)
}
