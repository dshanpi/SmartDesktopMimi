package kvm

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"io/fs"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/gin-gonic/gin"
)

// The platform updater owns this socket and the selected immutable asset path.
// Browser requests can select only these operations, never a URL or shell command.
func handleDesktopUpdate(c *gin.Context) {
	operation := c.Param("operation")
	method := http.MethodPost
	switch operation {
	case "status":
		method = http.MethodGet
	case "policy":
		method = http.MethodPut
	case "check", "download", "install", "cancel":
	default:
		c.JSON(404, gin.H{"error": "Unknown desktop update operation"})
		return
	}
	if c.Request.Method != method {
		c.Status(http.StatusMethodNotAllowed)
		return
	}
	transport := &http.Transport{DialContext: func(ctx context.Context, _, _ string) (net.Conn, error) {
		return (&net.Dialer{Timeout: 2 * time.Second}).DialContext(ctx, "unix", "/run/smartdesktopmimi/update.sock")
	}}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 4 * time.Second}
	var payload []byte
	if operation == "policy" {
		var err error
		payload, err = io.ReadAll(io.LimitReader(c.Request.Body, 2049))
		if err != nil || len(payload) > 2048 {
			c.Status(http.StatusBadRequest)
			return
		}
	}
	req, err := http.NewRequestWithContext(c.Request.Context(), method, "http://localhost/"+operation, bytes.NewReader(payload))
	if err != nil {
		c.Status(500)
		return
	}
	res, err := client.Do(req)
	if err != nil {
		c.JSON(http.StatusServiceUnavailable, gin.H{"error": "Desktop updates require an enrolled system baseline with the updater service"})
		return
	}
	defer res.Body.Close()
	body, err := io.ReadAll(io.LimitReader(res.Body, (1<<20)+1))
	if err != nil || len(body) > 1<<20 {
		c.Status(502)
		return
	}
	c.Data(res.StatusCode, res.Header.Get("Content-Type"), body)
}

type desktopAssetFS struct {
	fallback   fs.FS
	selectRoot func() string
}

func (d desktopAssetFS) Open(name string) (fs.File, error) {
	if !fs.ValidPath(name) {
		return nil, fs.ErrInvalid
	}
	root := d.selectRoot()
	if root == "" {
		return d.fallback.Open(name)
	}
	// Releases are immutable and root-owned. Reject all links within their www tree.
	current := root
	for _, part := range strings.Split(name, "/") {
		if part == "." {
			continue
		}
		current = filepath.Join(current, part)
		st, err := os.Lstat(current)
		if err != nil {
			return nil, err
		}
		if st.Mode()&os.ModeSymlink != 0 {
			return nil, fs.ErrPermission
		}
	}
	return os.Open(current)
}
func selectedDesktopWebRoot() string {
	if !isPlatformMode() {
		return ""
	}
	b, err := os.ReadFile("/run/smartdesktopmimi/web-root")
	if err != nil {
		return ""
	}
	root := strings.TrimSpace(string(b))
	if filepath.Clean(root) != root || !strings.HasSuffix(root, "/www") {
		return ""
	}
	if root != "/usr/lib/smartdesktopmimi/factory/www" {
		prefix := "/overlay/smartdesktopmimi/releases/"
		if !strings.HasPrefix(root, prefix) {
			return ""
		}
		id := strings.TrimSuffix(strings.TrimPrefix(root, prefix), "/www")
		if len(id) != 64 || strings.ContainsAny(id, "/\\") {
			return ""
		}
		for _, c := range id {
			if !(c >= 'a' && c <= 'f' || c >= '0' && c <= '9') {
				return ""
			}
		}
	}
	st, err := os.Lstat(root)
	if err != nil || !st.IsDir() || st.Mode()&os.ModeSymlink != 0 {
		return ""
	}
	return root
}

var desktopHealthOnce sync.Once

func startDesktopUpdateHealth() {
	if !isPlatformMode() {
		return
	}
	if _, err := os.Stat("/etc/smartdesktopmimi/updater.json"); err != nil {
		return
	}
	desktopHealthOnce.Do(func() {
		go func() {
			for {
				body, _ := json.Marshal(map[string]any{"pid": os.Getpid(), "unix_ms": time.Now().UnixMilli(), "busy": activeSessionCount() > 0})
				if err := os.WriteFile("/run/smartdesktopmimi/.web-health", body, 0600); err == nil {
					_ = os.Rename("/run/smartdesktopmimi/.web-health", "/run/smartdesktopmimi/web-health.json")
				}
				time.Sleep(time.Second)
			}
		}()
	})
}
