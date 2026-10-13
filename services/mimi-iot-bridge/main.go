package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"sync"
	"syscall"
	"time"
)

type Grant struct {
	UID               string   `json:"uid"`
	ControlCategories []string `json:"control_categories"`
}
type Config struct {
	Endpoint     string           `json:"endpoint"`
	AccessID     string           `json:"access_id"`
	AccessSecret string           `json:"access_secret"`
	Tokens       map[string]Grant `json:"token_sha256_grants"`
}
type API struct {
	Cloud  *Client
	Grants map[string]Grant
	mu     sync.Mutex
	last   map[string]time.Time
}

func loadConfig(path string) (Config, error) {
	var c Config
	fd, err := syscall.Open(path, syscall.O_RDONLY|syscall.O_NOFOLLOW|syscall.O_CLOEXEC, 0)
	if err != nil {
		return c, errors.New("cannot open private bridge configuration")
	}
	f := os.NewFile(uintptr(fd), path)
	defer f.Close()
	info, err := f.Stat()
	if err != nil || !info.Mode().IsRegular() || info.Mode().Perm()&0077 != 0 {
		return c, errors.New("bridge configuration must be a private regular file (0600)")
	}
	stat := info.Sys().(*syscall.Stat_t)
	if stat.Uid != uint32(os.Geteuid()) {
		return c, errors.New("configuration must be owned by the service user")
	}
	decoder := json.NewDecoder(io.LimitReader(f, 65537))
	decoder.DisallowUnknownFields()
	if decoder.Decode(&c) != nil || decoder.Decode(new(any)) != io.EOF {
		return c, errors.New("invalid bridge configuration")
	}
	if !regions[c.Endpoint] || !identifier.MatchString(c.AccessID) || c.AccessSecret == "" || len(c.Tokens) == 0 || len(c.Tokens) > 1000 {
		return c, errors.New("missing cloud region, credentials or per-device grants")
	}
	for hash, g := range c.Tokens {
		raw, e := hex.DecodeString(hash)
		if e != nil || len(raw) != 32 || hash != strings.ToLower(hash) || !identifier.MatchString(g.UID) {
			return c, errors.New("invalid device token grant")
		}
	}
	return c, nil
}
func respond(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(value)
}
func (a *API) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	auth := r.Header.Get("Authorization")
	if !strings.HasPrefix(auth, "Bearer ") || len(auth) < 39 || len(auth) > 256 {
		respond(w, 401, map[string]string{"error": "authorization_required"})
		return
	}
	hash := sha256.Sum256([]byte(strings.TrimPrefix(auth, "Bearer ")))
	key := hex.EncodeToString(hash[:])
	grant, ok := a.Grants[key]
	if !ok {
		respond(w, 401, map[string]string{"error": "authorization_required"})
		return
	}
	a.mu.Lock()
	if a.last == nil {
		a.last = map[string]time.Time{}
	}
	tooFast := time.Since(a.last[key]) < 250*time.Millisecond
	if !tooFast {
		a.last[key] = time.Now()
	}
	a.mu.Unlock()
	if tooFast {
		respond(w, 429, map[string]string{"error": "rate_limited"})
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()
	if r.URL.RawQuery != "" {
		respond(w, 400, map[string]string{"error": "query_not_supported"})
		return
	}
	parts := strings.Split(strings.Trim(r.URL.Path, "/"), "/")
	if len(parts) < 2 || parts[0] != "v1" || parts[1] != "devices" || len(parts) > 4 {
		respond(w, 404, map[string]string{"error": "not_found"})
		return
	}
	if (len(parts) == 2 && r.Method != "GET") || (len(parts) == 4 && parts[3] == "functions" && r.Method != "GET") || (len(parts) == 4 && parts[3] == "commands" && r.Method != "POST") {
		respond(w, 405, map[string]string{"error": "method_not_allowed"})
		return
	}
	devices, err := a.Cloud.Devices(ctx, grant.UID)
	if err != nil {
		respond(w, 502, map[string]string{"error": "cloud_unavailable"})
		return
	}
	if len(parts) == 2 {
		respond(w, 200, map[string]any{"devices": devices})
		return
	}
	var selected *Device
	for i := range devices {
		if devices[i].ID == parts[2] {
			selected = &devices[i]
			break
		}
	}
	if selected == nil {
		respond(w, 404, map[string]string{"error": "device_not_in_account"})
		return
	}
	if len(parts) == 4 && parts[3] == "functions" {
		f, err := a.Cloud.Functions(ctx, selected.ID)
		if err != nil {
			respond(w, 502, map[string]string{"error": "cloud_unavailable"})
			return
		}
		allowed := false
		for _, category := range grant.ControlCategories {
			if category == selected.Category {
				allowed = true
			}
		}
		respond(w, 200, map[string]any{"functions": f, "control_allowed": allowed && selected.Online})
		return
	}
	if len(parts) != 4 || parts[3] != "commands" {
		respond(w, 404, map[string]string{"error": "not_found"})
		return
	}
	allowed := false
	for _, category := range grant.ControlCategories {
		if category == selected.Category {
			allowed = true
		}
	}
	if !allowed {
		respond(w, 403, map[string]string{"error": "category_read_only"})
		return
	}
	if !selected.Online {
		respond(w, 409, map[string]string{"error": "device_offline"})
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, 4096)
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	var cmd Command
	if decoder.Decode(&cmd) != nil || decoder.Decode(new(any)) != io.EOF {
		respond(w, 400, map[string]string{"error": "invalid_command"})
		return
	}
	if err = a.Cloud.Send(ctx, selected.ID, cmd); err != nil {
		respond(w, 422, map[string]string{"error": "command_not_accepted"})
		return
	}
	// Acceptance is not device state. The desktop must refresh before showing
	// the requested state as achieved; timed-out commands are never auto-retried.
	respond(w, 200, map[string]bool{"accepted": true, "state_confirmed": false})
}
func main() {
	config := flag.String("config", "", "private server configuration file")
	listen := flag.String("listen", "127.0.0.1:9845", "TLS listen address")
	cert := flag.String("tls-cert", "", "TLS certificate file")
	key := flag.String("tls-key", "", "TLS private key file")
	flag.Parse()
	c, err := loadConfig(*config)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if *cert == "" || *key == "" {
		fmt.Fprintln(os.Stderr, "TLS certificate and key are required")
		os.Exit(1)
	}
	client := &Client{Base: c.Endpoint, ID: c.AccessID, Secret: c.AccessSecret, HTTP: &http.Client{Timeout: 15 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return errors.New("redirect refused") }}}
	api := &API{Cloud: client, Grants: c.Tokens}
	server := &http.Server{Addr: *listen, Handler: api, ReadHeaderTimeout: 5 * time.Second, ReadTimeout: 10 * time.Second, WriteTimeout: 35 * time.Second, IdleTimeout: 30 * time.Second, MaxHeaderBytes: 8192}
	if err = server.ListenAndServeTLS(*cert, *key); err != nil {
		fmt.Fprintln(os.Stderr, "bridge stopped:", err)
		os.Exit(1)
	}
}
