package update

import (
	"bufio"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

type EnrollmentEvidence struct {
	Kind   string `json:"kind"`
	Path   string `json:"path"`
	SHA256 string `json:"sha256"`
}

type Enrollment struct {
	Schema         int                  `json:"schema_version"`
	EnrollmentID   string               `json:"enrollment_id"`
	IdentitySHA256 string               `json:"identity_sha256"`
	Evidence       []EnrollmentEvidence `json:"evidence"`
}
type UIHealth struct {
	PID     int    `json:"pid"`
	Tick    uint64 `json:"monotonic_ms"`
	Backend uint64 `json:"backend_reply_ms"`
	Ready   bool   `json:"activity_ready"`
	Busy    bool   `json:"busy"`
}

func uptimeMillis() (uint64, error) {
	b, e := os.ReadFile("/proc/uptime")
	if e != nil {
		return 0, e
	}
	fields := strings.Fields(string(b))
	if len(fields) == 0 {
		return 0, fmt.Errorf("missing uptime")
	}
	v, e := strconv.ParseFloat(fields[0], 64)
	return uint64(v * 1000), e
}
func readHealth() (UIHealth, error) {
	var h UIHealth
	if e := ReadJSON("/run/smartdesktopmimi/ui-health.json", &h); e != nil {
		return h, e
	}
	now, e := uptimeMillis()
	if e != nil {
		return h, e
	}
	return h, validateUIHealth(h, now)
}

func validateUIHealth(h UIHealth, now uint64) error {
	if h.PID <= 1 || h.Tick == 0 || h.Backend == 0 || h.Tick > now || h.Backend > now || now-h.Tick > 4000 || now-h.Backend > 5000 {
		return fmt.Errorf("desktop/backend readiness is stale")
	}
	if !h.Ready {
		return fmt.Errorf("desktop activity status is incomplete")
	}
	return nil
}
func platformIdentity(c Config) error {
	if e := trustedFile(c.DeviceFile); e != nil {
		return e
	}
	var d Device
	if e := ReadJSON(c.DeviceFile, &d); e != nil {
		return e
	}
	if e := trustedFile(c.EnrollmentFile); e != nil {
		return e
	}
	var record Enrollment
	if e := ReadJSON(c.EnrollmentFile, &record); e != nil {
		return e
	}
	if d.Identity["platform_id"] == "ubuntu-host" {
		return validateUbuntuEnrollment(d, record)
	}
	d.Enrollment = record.EnrollmentID
	if e := d.Validate(); e != nil {
		return e
	}
	hardware := map[string]string{}
	for _, key := range identityKeys[:7] {
		hardware[key] = d.Identity[key]
	}
	raw, e := json.Marshal(hardware)
	if e != nil {
		return e
	}
	digest := sha256.Sum256(raw)
	if record.Schema != 1 || record.IdentitySHA256 != hex.EncodeToString(digest[:]) {
		return fmt.Errorf("hardware identity differs from controlled enrollment")
	}
	kinds := map[string]bool{}
	for _, proof := range record.Evidence {
		if proof.Kind != "board" && proof.Kind != "silicon" {
			return fmt.Errorf("unsupported enrollment evidence")
		}
		if !sha256Pattern.MatchString(proof.SHA256) {
			return fmt.Errorf("invalid enrollment hash")
		}
		real, e := filepath.EvalSymlinks(proof.Path)
		if e != nil {
			return e
		}
		if !allowedEnrollmentEvidencePath(d.Identity["platform_id"], proof.Kind, real) {
			return fmt.Errorf("identity evidence must come from kernel hardware nodes")
		}
		f, e := os.Open(real)
		if e != nil {
			return e
		}
		b, e := io.ReadAll(io.LimitReader(f, 65537))
		f.Close()
		if e != nil {
			return e
		}
		if len(b) == 0 || len(b) > 65536 {
			return fmt.Errorf("invalid hardware evidence size")
		}
		h := sha256.Sum256(b)
		if hex.EncodeToString(h[:]) != proof.SHA256 {
			return fmt.Errorf("hardware enrollment mismatch: %s", proof.Kind)
		}
		kinds[proof.Kind] = true
	}
	if !kinds["board"] || !kinds["silicon"] {
		return fmt.Errorf("board and silicon evidence are both required")
	}
	return nil
}

// Tina exposes its silicon ID as a class attribute, not under /sys/devices.
// Permit that exact kernel node for A133 silicon evidence only. Never broaden
// this exception to arbitrary files or to user-provided enrollment data.
func allowedEnrollmentEvidencePath(platform, kind, path string) bool {
	if platform == "a133" && kind == "silicon" && path == "/sys/class/sunxi_info/sys_info" {
		return true
	}
	return strings.HasPrefix(path, "/sys/devices/") || strings.HasPrefix(path, "/sys/firmware/devicetree/")
}

func platformIdle() error {
	h, e := readHealth()
	if e != nil {
		return e
	}
	if !h.Ready || h.Busy {
		return fmt.Errorf("voice/media/display is busy or status is incomplete")
	}
	var web struct {
		PID  int   `json:"pid"`
		Time int64 `json:"unix_ms"`
		Busy bool  `json:"busy"`
	}
	if e = ReadJSON("/run/smartdesktopmimi/web-health.json", &web); e != nil {
		return e
	}
	age := time.Now().UnixMilli() - web.Time
	if age < 0 || age > 5000 || web.PID <= 1 || web.Busy {
		return fmt.Errorf("browser control is busy or status is stale")
	}
	conn, e := net.DialTimeout("unix", "/var/run/aitvbox/control.sock", time.Second)
	if e != nil {
		return e
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(time.Second))
	if _, e = io.WriteString(conn, "{\"command\":\"status\"}\n"); e != nil {
		return e
	}
	b, e := bufio.NewReader(io.LimitReader(conn, 96<<10)).ReadBytes('\n')
	if e != nil {
		return e
	}
	var status struct {
		OK    bool   `json:"ok"`
		State string `json:"state"`
	}
	if e = json.Unmarshal(b, &status); e != nil {
		return e
	}
	if !status.OK {
		return fmt.Errorf("control status unavailable")
	}
	switch status.State {
	case "idle", "succeeded", "failed":
		return nil
	}
	return fmt.Errorf("device control task is active or unknown")
}
func platformStop() error {
	var h UIHealth
	_ = ReadJSON("/run/smartdesktopmimi/ui-health.json", &h)
	err := ubus("delete", map[string]any{"name": "mimi-desktop"})
	if h.PID <= 1 {
		return err
	}
	for i := 0; i < 80; i++ {
		if _, e := os.Stat(fmt.Sprintf("/proc/%d", h.PID)); os.IsNotExist(e) {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	return fmt.Errorf("previous UI did not stop: %v", err)
}
func ubus(method string, value any) error {
	b, e := json.Marshal(value)
	if e != nil {
		return e
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	return exec.CommandContext(ctx, "/bin/ubus", "call", "service", method, string(b)).Run()
}
func platformStart(dir string) error {
	if err := os.MkdirAll("/run/smartdesktopmimi", 0700); err != nil {
		return err
	}
	// Only a system provider may activate assets; application archives never write this path.
	web := filepath.Join(dir, "www")
	if _, err := os.Stat(filepath.Join(web, "index.html")); err == nil {
		tmp := "/run/smartdesktopmimi/.web-root"
		if err = os.WriteFile(tmp, []byte(web+"\n"), 0600); err != nil {
			return err
		}
		if err = os.Rename(tmp, "/run/smartdesktopmimi/web-root"); err != nil {
			return err
		}
	} else if !os.IsNotExist(err) {
		return err
	} else {
		if err = os.Remove("/run/smartdesktopmimi/web-root"); err != nil && !os.IsNotExist(err) {
			return err
		}
	}
	return ubus("set", map[string]any{"name": "mimi-desktop", "instances": map[string]any{"ui": map[string]any{"command": []string{"/usr/bin/aitvbox-start-ui"}, "env": map[string]string{"MIMI_RELEASE_DIR": dir}, "respawn": map[string]int{"threshold": 5, "timeout": 10, "retry": 3}, "stdout": true, "stderr": true}}})
}
func platformHealth(dir string) error {
	last := uint64(0)
	consecutive := 0
	for i := 0; i < 55; i++ {
		h, e := readHealth()
		if e == nil {
			exe, err := os.Readlink(fmt.Sprintf("/proc/%d/exe", h.PID))
			if err == nil && exe == filepath.Join(dir, "bin/lvglsim") && h.Tick > last {
				last = h.Tick
				consecutive++
			} else {
				consecutive = 0
			}
			if consecutive >= 3 {
				return platformWebHealth(dir)
			}
		} else {
			consecutive = 0
		}
		time.Sleep(time.Second)
	}
	return fmt.Errorf("UI event loop/backend handshake did not become healthy")
}
func PlatformOperation(c Config, args []string) error {
	if len(args) == 0 {
		return fmt.Errorf("missing provider operation")
	}
	if c.UbuntuSessionFile != "" && args[0] != "identity" && args[0] != "storage-health" {
		return ubuntuOperation(c, args)
	}
	switch args[0] {
	case "storage-health":
		if len(args) != 1 {
			return fmt.Errorf("unexpected arguments")
		}
		_, err := StorageHealth(c.Root)
		return err
	case "identity":
		if len(args) != 2 || args[1] != c.DeviceFile {
			return fmt.Errorf("unexpected identity source")
		}
		return platformIdentity(c)
	case "idle":
		if len(args) != 1 {
			return fmt.Errorf("unexpected arguments")
		}
		return platformIdle()
	case "stop":
		if len(args) != 1 {
			return fmt.Errorf("unexpected arguments")
		}
		return platformStop()
	case "start", "health":
		if len(args) != 2 {
			return fmt.Errorf("release path required")
		}
		dir := args[1]
		if dir != c.Factory {
			prefix := filepath.Join(c.Root, "releases") + "/"
			if !strings.HasPrefix(dir, prefix) || !sha256Pattern.MatchString(strings.TrimPrefix(dir, prefix)) {
				return fmt.Errorf("invalid selected release")
			}
		}
		if args[0] == "start" {
			return platformStart(dir)
		}
		return platformHealth(dir)
	default:
		return fmt.Errorf("unknown provider operation")
	}
}

func platformWebHealth(dir string) error {
	index := filepath.Join(dir, "www/index.html")
	if _, err := os.Stat(index); os.IsNotExist(err) {
		return nil
	} else if err != nil {
		return err
	}
	expected, _, err := hashFile(index)
	if err != nil {
		return err
	}
	client := http.Client{Timeout: 3 * time.Second}
	res, err := client.Get("http://127.0.0.1/static/")
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if res.StatusCode != 200 {
		return fmt.Errorf("web health HTTP %d", res.StatusCode)
	}
	b, err := io.ReadAll(io.LimitReader(res.Body, 1<<20))
	if err != nil {
		return err
	}
	sum := sha256.Sum256(b)
	if hex.EncodeToString(sum[:]) != expected {
		return fmt.Errorf("web server is not serving the selected application")
	}
	return nil
}
