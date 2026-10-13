package update

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"math/big"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"time"
)

type Service struct {
	Config          Config
	Engine          *Engine
	GitHub          *GitHub
	mu              sync.Mutex
	busy            bool
	cancel          context.CancelFunc
	candidate       *Candidate
	archive         string
	phase           string
	lastError       string
	downloadedBytes int64
	totalBytes      int64
}

func NewService(c Config) (*Service, error) {
	if err := c.loadPolicy(); err != nil {
		return nil, err
	}
	g, e := NewGitHub(c.Repository)
	if e != nil {
		return nil, e
	}
	service := &Service{Config: c, Engine: c.Engine(), GitHub: g, phase: "idle"}
	g.Progress = func(n, total int64) {
		service.mu.Lock()
		service.downloadedBytes = n
		service.totalBytes = total
		service.mu.Unlock()
	}
	return service, nil
}
func (s *Service) check(ctx context.Context) error {
	d, e := s.Engine.Hooks.Device()
	if e != nil {
		return e
	}
	trust, e := s.Engine.Trust()
	if e != nil {
		return e
	}
	st, e := s.Engine.Status()
	if e != nil {
		return e
	}
	if st.Trial {
		return fmt.Errorf("trial recovery pending")
	}
	current, e := s.Engine.installed(st.Current, d)
	if e != nil {
		return e
	}
	floor := st.HighWater
	if floor < current.Sequence {
		floor = current.Sequence
	}
	c, e := s.GitHub.Check(ctx, d, trust, s.Config.Channel, current.Version, floor)
	if e != nil {
		return e
	}
	s.mu.Lock()
	s.candidate = c
	s.archive = ""
	s.mu.Unlock()
	return nil
}
func (s *Service) start(op string) error {
	s.mu.Lock()
	if s.busy {
		s.mu.Unlock()
		return fmt.Errorf("update operation active")
	}
	if op != "check" && s.candidate == nil {
		s.mu.Unlock()
		return fmt.Errorf("check for an update first")
	}
	if op == "install" && s.archive == "" {
		s.mu.Unlock()
		return fmt.Errorf("download first")
	}
	ctx, cancel := context.WithCancel(context.Background())
	s.busy = true
	s.phase = op
	if op == "download" {
		s.downloadedBytes = 0
		s.totalBytes = s.candidate.Manifest.PackageBytes
	}
	s.lastError = ""
	s.cancel = cancel
	c, a := s.candidate, s.archive
	s.mu.Unlock()
	go func() {
		defer cancel()
		var e error
		switch op {
		case "check":
			e = s.check(ctx)
		case "download":
			var file string
			file, e = s.GitHub.Download(ctx, c, filepath.Join(s.Config.Root, "downloads"))
			if e == nil {
				s.mu.Lock()
				s.archive = file
				s.mu.Unlock()
			}
		case "install":
			e = s.Engine.Install(a, c.Raw, c.Signature, s.Config.Channel)
		}
		s.mu.Lock()
		defer func() {
			autoDownload := e == nil && op == "check" && s.Config.AutoDownload && s.candidate != nil
			s.mu.Unlock()
			if autoDownload {
				_ = s.start("download")
			}
		}()
		s.busy = false
		s.cancel = nil
		if e != nil {
			s.phase = "error"
			s.lastError = e.Error()
		} else {
			s.phase = "idle"
			if op == "install" {
				s.candidate = nil
				s.archive = ""
				os.Remove(a)
			}
		}
	}()
	return nil
}
func (s *Service) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("PUT /policy", s.policyHandler)
	mux.HandleFunc("GET /status", func(w http.ResponseWriter, r *http.Request) {
		s.mu.Lock()
		reason := s.lastError
		var candidate any
		if s.candidate != nil {
			candidate = map[string]any{"manifest": map[string]any{"version": s.candidate.Manifest.Version, "sequence": s.candidate.Manifest.Sequence}}
		}
		// Snapshot only in-memory fields under the operation lock. Slow storage
		// reads or HTTP clients must not block cancellation or download progress.
		bodyFields := map[string]any{"download_bytes": s.downloadedBytes, "total_bytes": s.totalBytes, "phase": s.phase, "busy": s.busy, "candidate": candidate, "downloaded": s.archive != "", "channel": s.Config.Channel, "policy": s.Config.policy()}
		s.mu.Unlock()
		state, e := s.Engine.Status()
		metadata, metadataError := s.Engine.CurrentMetadata()
		if reason == "" && metadataError != nil {
			reason = "System baseline is not enrolled or the factory application is unavailable."
		}
		if e != nil {
			reason = e.Error()
		}
		// UI polling must not serialize a package's potentially 20,000-file inventory.
		if len(state.History) > 10 {
			state.History = state.History[len(state.History)-10:]
		}
		w.Header().Set("Content-Type", "application/json")
		bodyFields["state"], bodyFields["current_version"], bodyFields["error"] = state, metadata.Version, reason
		body, err := json.Marshal(bodyFields)
		if err != nil {
			http.Error(w, "status encoding failed", 500)
			return
		}
		w.Header().Set("Content-Length", strconv.Itoa(len(body)))
		w.Write(body)
	})
	for _, op := range []string{"check", "download", "install"} {
		op := op
		mux.HandleFunc("POST /"+op, func(w http.ResponseWriter, r *http.Request) {
			if e := s.start(op); e != nil {
				http.Error(w, e.Error(), 409)
				return
			}
			w.WriteHeader(202)
		})
	}
	mux.HandleFunc("POST /cancel", func(w http.ResponseWriter, r *http.Request) {
		s.mu.Lock()
		defer s.mu.Unlock()
		if s.busy && s.phase != "install" {
			s.cancel()
			w.WriteHeader(202)
			return
		}
		http.Error(w, "no cancellable operation; activation cannot be cancelled", 409)
	})
	return mux
}
func (s *Service) Serve(ctx context.Context) error {
	if e := os.MkdirAll(filepath.Dir(s.Config.Socket), 0700); e != nil {
		return e
	}
	if st, e := os.Lstat(s.Config.Socket); e == nil {
		if st.Mode()&os.ModeSocket == 0 {
			return fmt.Errorf("socket path occupied")
		}
		c, e := net.DialTimeout("unix", s.Config.Socket, time.Second)
		if e == nil {
			c.Close()
			return fmt.Errorf("update service already running")
		}
		if e = os.Remove(s.Config.Socket); e != nil {
			return e
		}
	} else if !os.IsNotExist(e) {
		return e
	}
	listener, e := net.Listen("unix", s.Config.Socket)
	if e != nil {
		return e
	}
	defer listener.Close()
	defer os.Remove(s.Config.Socket)
	if e = os.Chmod(s.Config.Socket, 0600); e != nil {
		return e
	}
	srv := &http.Server{Handler: s.Handler(), ReadHeaderTimeout: 3 * time.Second, ReadTimeout: 5 * time.Second, WriteTimeout: 10 * time.Second, IdleTimeout: 10 * time.Second, MaxHeaderBytes: 4096}
	go func() {
		<-ctx.Done()
		shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		srv.Shutdown(shutdown)
	}()
	go func() {
		first := true
		for {
			n, _ := rand.Int(rand.Reader, big.NewInt(3600))
			jitter := time.Duration(0)
			if n != nil {
				jitter = time.Duration(n.Int64()) * time.Second
			}
			delay := 24*time.Hour + jitter
			if first {
				delay = 30*time.Second + jitter/60
				first = false
			}
			timer := time.NewTimer(delay)
			select {
			case <-ctx.Done():
				timer.Stop()
				return
			case <-timer.C:
				s.start("check")
			}
		}
	}()
	{
		go func() {
			ticker := time.NewTicker(time.Minute)
			defer ticker.Stop()
			for {
				select {
				case <-ctx.Done():
					return
				case <-ticker.C:
					s.mu.Lock()
					ready := s.Config.InstallWhenIdle && !s.busy && s.archive != "" && s.phase != "error"
					s.mu.Unlock()
					if ready && s.Engine.Hooks.Idle() == nil {
						_ = s.start("install")
					}
				}
			}
		}()
	}
	e = srv.Serve(listener)
	if e == http.ErrServerClosed {
		return nil
	}
	return e
}
