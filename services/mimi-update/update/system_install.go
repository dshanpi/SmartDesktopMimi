package update

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

// Only exact, unambiguous A133 A/B roots are supported. Never trust environment
// alone, a substring match, or a partition list elsewhere in the command line.
func ABDirection(cmdline, environment string) (string, error) {
	root := ""
	for _, field := range strings.Fields(cmdline) {
		if strings.HasPrefix(field, "root=") {
			if root != "" {
				return "", fmt.Errorf("duplicate boot root")
			}
			root = strings.TrimPrefix(field, "root=")
			if root == "" {
				return "", fmt.Errorf("empty boot root")
			}
		}
	}
	switch {
	case root == "/dev/mmcblk0p4" && strings.TrimSpace(environment) == "bootA":
		return "now_A_next_B", nil
	case root == "/dev/mmcblk0p6" && strings.TrimSpace(environment) == "bootB":
		return "now_B_next_A", nil
	}
	return "", fmt.Errorf("actual boot root and A/B environment disagree or are unknown")
}

type SystemJournal struct {
	ProductEdition string `json:"product_edition,omitempty"`
	Schema         int    `json:"schema_version"`
	HighWater      uint64 `json:"high_water"`
	Phase          string `json:"phase"`
	Version        string `json:"version"`
	PackageSHA256  string `json:"package_sha256"`
	BootID         string `json:"boot_id,omitempty"`
}

func (e *Engine) reconcileSystem(d Device) (SystemJournal, error) {
	dir := filepath.Join(e.Root, "system")
	s, err := systemJournal(dir)
	if err != nil {
		return s, err
	}
	if s.Phase != "applying" && s.Phase != "pending-reboot" {
		return s, nil
	}
	boot, err := os.ReadFile("/proc/sys/kernel/random/boot_id")
	if err != nil {
		return s, err
	}
	if s.BootID == "" {
		return s, fmt.Errorf("system transaction lacks boot identity")
	}
	if strings.TrimSpace(string(boot)) != s.BootID && d.Runtime["system"] != s.Version {
		s.Phase = "failed" // Interrupted write or bootloader rollback; never retry its sequence.
		err = atomicJSON(dir, "state.json", s)
	}
	return s, err
}

// Called only inside SystemHealth's lock AFTER actual UI/backend health and
// successful boot environment confirmation. A crash is safe to retry.
func CommitSystemJournal(c Config) error {
	d, err := (Provider{c}).Device()
	if err != nil {
		return err
	}
	dir := filepath.Join(c.Root, "system")
	s, err := systemJournal(dir)
	if err != nil {
		return err
	}
	if err = validateSystemCommit(s, d); err != nil {
		return err
	}
	if s.Phase == "idle" || s.Phase == "committed" {
		return nil
	}
	if err = checkSystemProductCommit(c, s); err != nil {
		return err
	}
	s.Phase = "committed"
	return atomicJSON(dir, "state.json", s)
}

func validateSystemCommit(s SystemJournal, d Device) error {
	if s.Phase == "idle" || s.Phase == "committed" {
		return nil
	}
	if s.Version != d.Runtime["system"] || (s.Phase != "applying" && s.Phase != "pending-reboot") {
		return fmt.Errorf("system boot does not match pending transaction")
	}
	return nil
}

// Run BEFORE clearing bootloader rollback state, under the shared lock.
func CheckSystemCommit(c Config) error {
	d, err := (Provider{c}).Device()
	if err != nil {
		return err
	}
	s, err := systemJournal(filepath.Join(c.Root, "system"))
	if err != nil {
		return err
	}
	if err = validateSystemCommit(s, d); err != nil {
		return err
	}
	return checkSystemProductCommit(c, s)
}

func atomicJSON(dir, name string, v any) error {
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	raw, err := json.Marshal(v)
	if err != nil {
		return err
	}
	f, err := os.CreateTemp(dir, ".journal-")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
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
	if err = os.Rename(f.Name(), filepath.Join(dir, name)); err != nil {
		return err
	}
	return syncDir(dir)
}

func systemJournal(dir string) (SystemJournal, error) {
	s := SystemJournal{Schema: 1, Phase: "idle"}
	err := ReadJSON(filepath.Join(dir, "state.json"), &s)
	if os.IsNotExist(err) {
		return s, nil
	}
	if err != nil {
		return s, err
	}
	if s.Schema != 1 {
		return s, fmt.Errorf("unsupported system journal")
	}
	switch s.Phase {
	case "idle", "applying", "pending-reboot", "committed", "failed":
	default:
		return s, fmt.Errorf("invalid system journal phase")
	}
	return s, nil
}

func (c Config) systemDevice() (SystemDevice, error) {
	var d SystemDevice
	if c.SystemDeviceFile == "" || c.SystemRepository == "" {
		return d, fmt.Errorf("signed system update baseline is not provisioned")
	}
	if err := trustedFile(c.SystemDeviceFile); err != nil {
		return d, err
	}
	if err := ReadJSON(c.SystemDeviceFile, &d); err != nil {
		return d, err
	}
	if err := systemEditionMarker("/etc/smartdesktopmimi/product-edition", d.ProductEdition); err != nil {
		return d, err
	}
	live, err := (Provider{c}).Device()
	if err != nil {
		return d, err
	}
	// The system descriptor cannot substitute an unrelated identity/runtime file.
	d.Device.Enrollment = live.Enrollment
	a, _ := json.Marshal(live)
	b, _ := json.Marshal(d.Device)
	if !bytes.Equal(a, b) {
		return d, fmt.Errorf("system and application platform descriptors disagree")
	}
	if d.Device.Identity["soc_id"] != "a133" || d.Storage != "emmc" || d.BootMode != "ab" {
		return d, fmt.Errorf("system installer is implemented only for A133 eMMC A/B")
	}
	digest, capacity, err := A133Geometry()
	if err != nil {
		return d, err
	}
	if digest != d.PartitionSHA256 || capacity != d.StorageBytes {
		return d, fmt.Errorf("live eMMC geometry differs from system baseline")
	}
	return d, nil
}

type SystemProgress struct {
	Phase    string `json:"phase"`
	Version  string `json:"version"`
	Progress int    `json:"progress"`
}

func readSystemCandidate(path string, c *SystemCandidate) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	// Two signed manifests (each <=1 MiB) plus base64 overhead and signatures.
	raw, err := io.ReadAll(io.LimitReader(f, (4<<20)+1))
	if err != nil {
		return err
	}
	return strictJSONLimit(raw, c, 4<<20)
}

// SystemOperation is used by the CLI and the legacy desktop backend bridge.
// All entries use the same trust, board admission and application/system lock.
func SystemOperation(ctx context.Context, c Config, operation string, report func(SystemProgress)) error {
	engine := c.Engine()
	unlock, err := engine.lock()
	if err != nil {
		return err
	}
	defer unlock()
	if err = engine.Hooks.Preflight(); err != nil {
		return err
	}
	d, err := c.systemDevice()
	if err != nil {
		return err
	}
	trust, err := engine.Trust()
	if err != nil {
		return err
	}
	dir := filepath.Join(c.Root, "system")
	state, err := engine.reconcileSystem(d.Device)
	if err != nil {
		return err
	}
	if state.Phase == "applying" || state.Phase == "pending-reboot" {
		return fmt.Errorf("system transaction requires boot/recovery before another update")
	}
	github, err := NewGitHub(c.SystemRepository)
	if err != nil {
		return err
	}
	emit := func(phase, version string, pct int) {
		if report != nil {
			report(SystemProgress{phase, version, pct})
		}
	}
	if operation == "system-check" {
		emit("checking", d.Device.Runtime["system"], 0)
		candidate, err := github.CheckSystem(ctx, d, trust, c.Channel, state.HighWater)
		if err != nil {
			return err
		}
		if candidate == nil {
			if err = os.Remove(filepath.Join(dir, "candidate.json")); err != nil && !os.IsNotExist(err) {
				return err
			}
			emit("up-to-date", d.Device.Runtime["system"], 100)
			return nil
		}
		m, err := candidate.Verify(d, trust, c.Channel, state.HighWater)
		if err != nil {
			return err
		}
		if err = atomicJSON(dir, "candidate.json", candidate); err != nil {
			return err
		}
		emit("available", m.Version, 0)
		return nil
	}
	var candidate SystemCandidate
	if err = readSystemCandidate(filepath.Join(dir, "candidate.json"), &candidate); err != nil {
		return err
	}
	m, err := candidate.Verify(d, trust, c.Channel, state.HighWater)
	if err != nil {
		return err
	}
	if operation == "system-download" {
		github.Progress = func(n, total int64) { emit("downloading", m.Version, int(n*100/total)) }
		_, err = github.DownloadSystem(ctx, candidate, d, trust, c.Channel, state.HighWater, dir)
		if err == nil {
			emit("downloaded", m.Version, 100)
		}
		return err
	}
	if operation != "system-install" {
		return fmt.Errorf("unsupported system operation")
	}
	archive := filepath.Join(dir, m.OTA.PackageSHA256+".swu")
	hash, size, err := hashFile(archive)
	if err != nil {
		return err
	}
	if hash != m.OTA.PackageSHA256 || size != m.OTA.PackageBytes {
		return fmt.Errorf("downloaded system package is corrupt")
	}
	if err = engine.Hooks.Idle(); err != nil {
		return err
	}
	// Check actual root, environment, pending trial and signature before any write.
	cmdline, err := os.ReadFile("/proc/cmdline")
	if err != nil {
		return err
	}
	getEnv := func(key string) (string, error) {
		child, cancel := context.WithTimeout(ctx, 5*time.Second)
		defer cancel()
		b, e := exec.CommandContext(child, "/usr/sbin/fw_printenv", "-n", key).Output()
		return strings.TrimSpace(string(b)), e
	}
	env, err := getEnv("boot_partition")
	if err != nil {
		return err
	}
	direction, err := ABDirection(string(cmdline), env)
	if err != nil {
		return err
	}
	pending, err := getEnv("upgrade_available")
	if err != nil {
		return err
	}
	if pending != "0" {
		return fmt.Errorf("unconfirmed system boot")
	}
	if err = trustedFile("/etc/swupdate_public.pem"); err != nil {
		return err
	}
	// SWUpdate check-only validates its independent signature and selected payload.
	selector := "stable," + direction
	runSWU := func(check bool) error {
		child, cancel := context.WithTimeout(ctx, 20*time.Minute)
		defer cancel()
		args := []string{"-i", archive, "-e", selector, "-k", "/etc/swupdate_public.pem"}
		if check {
			args = append(args, "-c")
		}
		cmd := exec.CommandContext(child, "/sbin/swupdate", args...)
		cmd.SysProcAttr = &syscall.SysProcAttr{Pdeathsig: syscall.SIGTERM}
		cmd.Stdout = os.Stderr
		cmd.Stderr = os.Stderr
		return cmd.Run()
	}
	if err = runSWU(true); err != nil {
		return fmt.Errorf("SWUpdate signature/payload preflight failed: %w", err)
	}
	// Keeping the current application compatible with BOTH systems guarantees
	// that the same persistent data/release can survive an A/B rollback.
	appState, err := engine.readState()
	if err != nil {
		return err
	}
	if appState.Trial {
		return fmt.Errorf("application trial in progress")
	}
	next := m.OTA.Device
	next.Enrollment = d.Device.Enrollment
	if _, err = engine.installed(appState.Current, d.Device); err != nil {
		return err
	}
	if _, err = engine.installed(appState.Current, next); err != nil {
		return fmt.Errorf("install an application compatible with both system versions first: %w", err)
	}
	boot, err := os.ReadFile("/proc/sys/kernel/random/boot_id")
	if err != nil {
		return err
	}
	if err = engine.Hooks.Preflight(); err != nil {
		return err
	}
	state = SystemJournal{ProductEdition: m.ProductEdition, Schema: 1, HighWater: m.Sequence, Phase: "applying", Version: m.Version, PackageSHA256: m.OTA.PackageSHA256, BootID: strings.TrimSpace(string(boot))}
	if err = atomicJSON(dir, "state.json", state); err != nil {
		return err
	}
	emit("applying", m.Version, 10)
	if err = runSWU(false); err != nil {
		// A failing writer may already have changed boot environment. Keep the
		// transaction fenced until the next boot identifies the actual system.
		journalErr := atomicJSON(dir, "state.json", state)
		return fmt.Errorf("system installation failed: %v (journal: %v)", err, journalErr)
	}
	state.Phase = "pending-reboot"
	if err = atomicJSON(dir, "state.json", state); err != nil {
		return err
	}
	emit("pending-reboot", m.Version, 100)
	return nil
}

// The caller always supplies the fixed platform path, never request input.
func systemEditionMarker(path, declared string) error {
	info, err := os.Lstat(path)
	if os.IsNotExist(err) && declared == "" {
		return nil
	}
	if err != nil {
		return fmt.Errorf("system product edition marker unavailable: %w", err)
	}
	if !info.Mode().IsRegular() || info.Size() > 16 {
		return fmt.Errorf("invalid system product edition marker")
	}
	if err := trustedFile(path); err != nil {
		return err
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	return validateSystemEditionMarker(raw, declared)
}
func validateSystemEditionMarker(raw []byte, declared string) error {
	if declared != "public" && declared != "private" {
		return fmt.Errorf("system product edition descriptor missing or unknown")
	}
	if string(raw) != declared && string(raw) != declared+"\n" {
		return fmt.Errorf("system product edition marker disagrees with descriptor")
	}
	return nil
}

func checkSystemProductCommit(c Config, s SystemJournal) error {
	if s.Phase == "idle" || s.Phase == "committed" {
		return nil
	}
	if s.ProductEdition == "" {
		// A new product rootfs cannot confirm a legacy unpaired transaction.
		return systemEditionMarker("/etc/smartdesktopmimi/product-edition", "")
	}
	d, err := c.systemDevice()
	if err != nil {
		return err
	}
	return matchSystemEditions(s.ProductEdition, d.ProductEdition)
}
