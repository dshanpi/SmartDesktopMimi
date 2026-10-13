package update

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

type Config struct {
	Schema           int    `json:"schema_version"`
	Root             string `json:"root"`
	Factory          string `json:"factory"`
	Lock             string `json:"lock"`
	Socket           string `json:"socket"`
	DeviceFile       string `json:"device_file"`
	TrustFile        string `json:"trust_file"`
	Provider         string `json:"provider"`
	Repository       string `json:"repository"`
	SystemRepository string `json:"system_repository,omitempty"`
	SystemDeviceFile string `json:"system_device_file,omitempty"`
	Channel          string `json:"channel"`
	EnrollmentFile   string `json:"enrollment_file"`
	AutoDownload     bool   `json:"auto_download"`
	InstallWhenIdle  bool   `json:"install_when_idle"`
}

func ReadJSON(p string, out any) error {
	b, e := os.ReadFile(p)
	if e != nil {
		return e
	}
	return StrictJSON(b, out)
}
func trustedFile(p string) error {
	if !filepath.IsAbs(p) || filepath.Clean(p) != p {
		return fmt.Errorf("trusted path must be absolute and clean")
	}
	// Reject symlinks and group/other-writable system configuration, including ancestors.
	for cur := p; ; cur = filepath.Dir(cur) {
		s, e := os.Lstat(cur)
		if e != nil {
			return e
		}
		st, ok := s.Sys().(*syscall.Stat_t)
		if !ok || st.Uid != 0 || s.Mode()&os.ModeSymlink != 0 || s.Mode().Perm()&0022 != 0 {
			return fmt.Errorf("untrusted root-owned path %s", cur)
		}
		if cur == "/" {
			break
		}
	}
	return nil
}
func LoadConfig(p string) (Config, error) {
	var c Config
	if e := trustedFile(p); e != nil {
		return c, e
	}
	if e := ReadJSON(p, &c); e != nil {
		return c, e
	}
	if c.Schema != 1 || (c.Channel != "stable" && c.Channel != "testing") {
		return c, fmt.Errorf("unsupported configuration")
	}
	for _, p := range []string{c.Root, c.Factory, c.Lock, c.Socket, c.DeviceFile, c.TrustFile, c.Provider, c.EnrollmentFile} {
		if !filepath.IsAbs(p) || filepath.Clean(p) != p {
			return c, fmt.Errorf("invalid configured path")
		}
	}
	if _, e := NewGitHub(c.Repository); e != nil {
		return c, e
	}
	if e := c.validateSystemConfig(); e != nil {
		return c, e
	}
	if err := c.loadPolicy(); err != nil {
		return c, err
	}
	return c, nil
}

type Provider struct{ Config Config }

func (p Provider) run(timeout time.Duration, args ...string) ([]byte, error) {
	if e := trustedFile(p.Config.Provider); e != nil {
		return nil, e
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	cmd := exec.CommandContext(ctx, p.Config.Provider, args...)
	b, e := cmd.Output()
	if e != nil {
		if exit, ok := e.(*exec.ExitError); ok {
			detail := strings.TrimSpace(string(exit.Stderr))
			if len(detail) > 2048 {
				detail = detail[:2048]
			}
			if detail != "" {
				return nil, fmt.Errorf("platform provider %s failed: %w: %s", args[0], e, detail)
			}
		}
		return nil, fmt.Errorf("platform provider %s failed: %w", args[0], e)
	}
	return b, nil
}
func (p Provider) Device() (Device, error) {
	var d Device
	if e := trustedFile(p.Config.DeviceFile); e != nil {
		return d, e
	}
	if e := ReadJSON(p.Config.DeviceFile, &d); e != nil {
		return d, e
	}
	if e := trustedFile(p.Config.EnrollmentFile); e != nil {
		return d, e
	}
	var enrollment Enrollment
	if e := ReadJSON(p.Config.EnrollmentFile, &enrollment); e != nil {
		return d, e
	}
	d.Enrollment = enrollment.EnrollmentID
	if e := d.Validate(); e != nil {
		return d, e
	}
	// This must read board facts independently; comparing two files is not enrollment.
	_, e := p.run(5*time.Second, "identity", p.Config.DeviceFile)
	return d, e
}
func (p Provider) Preflight() error       { _, e := p.run(5*time.Second, "storage-health"); return e }
func (p Provider) Idle() error            { _, e := p.run(5*time.Second, "idle"); return e }
func (p Provider) Stop() error            { _, e := p.run(10*time.Second, "stop"); return e }
func (p Provider) Start(dir string) error { _, e := p.run(10*time.Second, "start", dir); return e }
func (p Provider) Healthy(dir string, timeout time.Duration) error {
	_, e := p.run(timeout, "health", dir)
	return e
}
func (c Config) Engine() *Engine {
	return &Engine{Root: c.Root, Factory: c.Factory, Lock: c.Lock, Hooks: Provider{c}, Trust: func() (Trust, error) {
		var t Trust
		if e := trustedFile(c.TrustFile); e != nil {
			return t, e
		}
		e := ReadJSON(c.TrustFile, &t)
		return t, e
	}}
}

func (c Config) validateSystemConfig() error {
	if c.SystemRepository != "" || c.SystemDeviceFile != "" {
		if _, e := NewGitHub(c.SystemRepository); e != nil {
			return e
		}
		if !filepath.IsAbs(c.SystemDeviceFile) || filepath.Clean(c.SystemDeviceFile) != c.SystemDeviceFile {
			return fmt.Errorf("invalid system update configuration")
		}
	}
	return nil
}
