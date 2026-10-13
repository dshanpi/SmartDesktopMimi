package update

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
)

// User policy cannot change platform identity, trust, repository or signing rules.
type UserPolicy struct {
	Schema          int    `json:"schema_version"`
	Channel         string `json:"channel"`
	AutoDownload    bool   `json:"auto_download"`
	InstallWhenIdle bool   `json:"install_when_idle"`
}

func (p UserPolicy) validate() error {
	if p.Schema != 1 || (p.Channel != "stable" && p.Channel != "testing") {
		return fmt.Errorf("unsupported update policy")
	}
	return nil
}
func (c Config) policy() UserPolicy {
	return UserPolicy{1, c.Channel, c.AutoDownload, c.InstallWhenIdle}
}
func (c *Config) usePolicy(p UserPolicy) {
	c.Channel = p.Channel
	c.AutoDownload = p.AutoDownload
	c.InstallWhenIdle = p.InstallWhenIdle
}
func (c *Config) loadPolicy() error {
	path := filepath.Join(c.Root, "policy.json")
	st, err := os.Lstat(path)
	if os.IsNotExist(err) {
		return nil
	}
	if err != nil {
		return err
	}
	if !st.Mode().IsRegular() || st.Mode().Perm()&0022 != 0 {
		return fmt.Errorf("untrusted policy file")
	}
	var p UserPolicy
	if err = ReadJSON(path, &p); err != nil {
		return err
	}
	if err = p.validate(); err != nil {
		return err
	}
	c.usePolicy(p)
	return nil
}
func savePolicy(root string, p UserPolicy) error {
	if err := p.validate(); err != nil {
		return err
	}
	if err := os.MkdirAll(root, 0700); err != nil {
		return err
	}
	b, err := json.Marshal(p)
	if err != nil {
		return err
	}
	f, err := os.CreateTemp(root, ".policy-")
	if err != nil {
		return err
	}
	name := f.Name()
	defer os.Remove(name)
	if _, err = f.Write(b); err == nil {
		err = f.Sync()
	}
	closeErr := f.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	if err = os.Rename(name, filepath.Join(root, "policy.json")); err != nil {
		return err
	}
	return syncDir(root)
}
func (s *Service) policyHandler(w http.ResponseWriter, r *http.Request) {
	body, err := io.ReadAll(io.LimitReader(r.Body, 2049))
	var p UserPolicy
	if err != nil || len(body) > 2048 || StrictJSON(body, &p) != nil || p.validate() != nil {
		http.Error(w, "invalid update policy", 400)
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.busy {
		http.Error(w, "update operation active", 409)
		return
	}
	if err = savePolicy(s.Config.Root, p); err != nil {
		http.Error(w, "policy could not be saved", 500)
		return
	}
	// A channel change invalidates the previously selected package immediately.
	if p.Channel != s.Config.Channel {
		if s.archive != "" {
			_ = os.Remove(s.archive)
		}
		s.candidate = nil
		s.archive = ""
		s.downloadedBytes = 0
		s.totalBytes = 0
	}
	s.Config.usePolicy(p)
	w.WriteHeader(http.StatusNoContent)
}
