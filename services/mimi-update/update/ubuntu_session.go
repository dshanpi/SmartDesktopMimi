package update

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"syscall"
	"time"
)

type UbuntuSession struct {
	Schema         int    `json:"schema_version"`
	LoginSession   string `json:"login_session,omitempty"`
	UID            uint32 `json:"uid"`
	GID            uint32 `json:"gid"`
	Unit           string `json:"unit"`
	Script         string `json:"script"`
	ScriptSHA256   string `json:"script_sha256"`
	Provider       string `json:"provider"`
	ProviderSHA256 string `json:"provider_sha256"`
	Runtime        string `json:"runtime"`
	Settings       string `json:"settings"`
	Screen         string `json:"screen"`
	Display        string `json:"display"`
	XAuthority     string `json:"xauthority"`
}

var ubuntuLoginID = regexp.MustCompile(`^[A-Za-z0-9]{1,32}$`)

var ubuntuUnit = regexp.MustCompile(`^mimi-ubuntu-[a-z0-9-]{1,32}\.service$`)
var ubuntuDisplay = regexp.MustCompile(`^:[0-9]{1,5}(?:\.[0-9])?$`)
var ubuntuScreen = regexp.MustCompile(`^[1-9][0-9]{2,3}x[1-9][0-9]{2,3}$`)

func (s UbuntuSession) validate() error {
	if s.LoginSession != "" && !ubuntuLoginID.MatchString(s.LoginSession) {
		return fmt.Errorf("invalid graphical login session")
	}
	if s.Schema != 1 || s.UID == 0 || s.GID == 0 || !ubuntuUnit.MatchString(s.Unit) || !ubuntuDisplay.MatchString(s.Display) || !ubuntuScreen.MatchString(s.Screen) {
		return fmt.Errorf("invalid Ubuntu session scope")
	}
	for _, p := range []string{s.Script, s.Provider, s.Runtime, s.Settings, s.XAuthority} {
		if !filepath.IsAbs(p) || filepath.Clean(p) != p || strings.ContainsAny(p, "\x00\r\n\t") {
			return fmt.Errorf("invalid Ubuntu session path")
		}
	}
	if !sha256Pattern.MatchString(s.ScriptSHA256) || !sha256Pattern.MatchString(s.ProviderSHA256) || filepath.Base(s.Settings) != "settings.json" {
		return fmt.Errorf("invalid Ubuntu session input binding")
	}
	if s.Runtime == filepath.Dir(s.Settings) || strings.HasPrefix(s.Settings, s.Runtime+"/") || s.Script == s.Provider || strings.HasPrefix(s.Script, s.Runtime+"/") || strings.HasPrefix(s.Provider, s.Runtime+"/") {
		return fmt.Errorf("overlapping Ubuntu session paths")
	}
	if len(s.Runtime+"/ipc.sock.v2") >= 108 {
		return fmt.Errorf("Ubuntu socket path too long")
	}
	return nil
}
func loadUbuntuSession(c Config) (UbuntuSession, string, error) {
	var s UbuntuSession
	if err := trustedFile(c.UbuntuSessionFile); err != nil {
		return s, "", err
	}
	if err := ReadJSON(c.UbuntuSessionFile, &s); err != nil {
		return s, "", err
	}
	if err := s.validate(); err != nil {
		return s, "", err
	}
	hash, _, err := hashFile(c.UbuntuSessionFile)
	return s, "SmartDesktopMimi Ubuntu " + hash, err
}
func privateUserDirectory(path string, uid uint32) error {
	if err := trustedFile(filepath.Dir(path)); err != nil {
		return err
	}
	st, err := os.Lstat(path)
	if err != nil {
		return err
	}
	sys, ok := st.Sys().(*syscall.Stat_t)
	if !ok || !st.IsDir() || st.Mode().Perm() != 0700 || sys.Uid != uid {
		return fmt.Errorf("invalid private desktop-user directory")
	}
	return nil
}
func trustedPinnedFile(path, hash string) error {
	if err := trustedFile(path); err != nil {
		return err
	}
	st, err := os.Stat(path)
	if err != nil || !st.Mode().IsRegular() {
		return fmt.Errorf("trusted program is not regular")
	}
	actual, _, err := hashFile(path)
	if err != nil {
		return err
	}
	if actual != hash {
		return fmt.Errorf("trusted Ubuntu component changed")
	}
	return nil
}
func ubuntuProgram(path string) (string, error) {
	real, err := filepath.EvalSymlinks(path)
	if err != nil {
		return "", err
	}
	if err = trustedFile(real); err != nil {
		return "", err
	}
	return real, nil
}
func (s UbuntuSession) environment() ([]string, error) {
	u, err := user.LookupId(strconv.FormatUint(uint64(s.UID), 10))
	if err != nil {
		return nil, err
	}
	if u.Gid != strconv.FormatUint(uint64(s.GID), 10) || !filepath.IsAbs(u.HomeDir) {
		return nil, fmt.Errorf("desktop-user identity differs")
	}
	return []string{"PATH=/usr/bin:/bin", "LANG=C.UTF-8", "HOME=" + u.HomeDir, "DISPLAY=" + s.Display, "XAUTHORITY=" + s.XAuthority}, nil
}
func checkUbuntuLoginProperties(s UbuntuSession, values map[string]string) error {
	expected := map[string]string{"Id": s.LoginSession, "User": strconv.FormatUint(uint64(s.UID), 10), "Class": "user", "Type": "x11", "Remote": "no", "Active": "yes", "Seat": "seat0", "Display": s.Display, "Scope": "session-" + s.LoginSession + ".scope"}
	for k, v := range expected {
		if values[k] != v {
			return fmt.Errorf("graphical login scope differs: %s", k)
		}
	}
	return nil
}
func (s UbuntuSession) readyLogin() error {
	if s.LoginSession == "" {
		return nil
	}
	program, err := ubuntuProgram("/usr/bin/loginctl")
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	args := []string{"show-session", s.LoginSession}
	for _, key := range []string{"Id", "User", "Class", "Type", "Remote", "Active", "Seat", "Display", "Scope"} {
		args = append(args, "--property="+key)
	}
	cmd := exec.CommandContext(ctx, program, args...)
	cmd.Env = []string{"PATH=/usr/bin:/bin", "LANG=C"}
	raw, err := cmd.Output()
	if err != nil {
		return fmt.Errorf("graphical login no longer available")
	}
	values, err := unitProperties(raw)
	if err != nil {
		return err
	}
	return checkUbuntuLoginProperties(s, values)
}
func (s UbuntuSession) readyInputs() error {
	if err := s.readyLogin(); err != nil {
		return err
	}
	if err := trustedPinnedFile(s.Script, s.ScriptSHA256); err != nil {
		return err
	}
	if err := trustedPinnedFile(s.Provider, s.ProviderSHA256); err != nil {
		return err
	}
	for _, p := range []string{s.Runtime, filepath.Dir(s.Settings)} {
		if err := privateUserDirectory(p, s.UID); err != nil {
			return err
		}
	}
	if err := trustedFile(filepath.Dir(s.XAuthority)); err != nil {
		return err
	}
	f, err := os.OpenFile(s.XAuthority, os.O_RDONLY|syscall.O_NOFOLLOW, 0)
	if err != nil {
		return err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return err
	}
	sys, ok := st.Sys().(*syscall.Stat_t)
	if !ok || !st.Mode().IsRegular() || st.Mode().Perm() != 0600 || sys.Uid != s.UID || sys.Nlink != 1 || st.Size() == 0 || st.Size() > 65536 {
		return fmt.Errorf("invalid local display authorization")
	}
	return nil
}
func ubuntuSystemctl(args ...string) ([]byte, error) {
	p, err := ubuntuProgram("/usr/bin/systemctl")
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 7*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, p, args...)
	cmd.Env = []string{"PATH=/usr/bin:/bin", "LANG=C"}
	return cmd.Output()
}
func unitProperties(raw []byte) (map[string]string, error) {
	values := map[string]string{}
	for _, line := range strings.Split(strings.TrimSpace(string(raw)), "\n") {
		if line == "" {
			continue
		}
		p := strings.SplitN(line, "=", 2)
		if len(p) != 2 {
			return nil, fmt.Errorf("invalid systemd state")
		}
		if _, ok := values[p[0]]; ok {
			return nil, fmt.Errorf("duplicate systemd state")
		}
		values[p[0]] = p[1]
	}
	return values, nil
}
func ownedUbuntuUnit(s UbuntuSession, description string) (map[string]string, error) {
	raw, err := ubuntuSystemctl("show", s.Unit, "--property=LoadState,ActiveState,MainPID,Description,Transient,User")
	state, parseErr := unitProperties(raw)
	if parseErr != nil {
		return nil, parseErr
	}
	if state["LoadState"] == "not-found" {
		return state, nil
	}
	if err != nil {
		return nil, err
	}
	if state["Description"] != description || state["Transient"] != "yes" || state["User"] != strconv.FormatUint(uint64(s.UID), 10) {
		return nil, fmt.Errorf("systemd unit is not owned by this platform configuration")
	}
	return state, nil
}
func ubuntuRelease(c Config, dir string) (string, error) {
	if dir == c.Factory {
		return "factory", nil
	}
	prefix := filepath.Join(c.Root, "releases") + "/"
	if !strings.HasPrefix(dir, prefix) || !sha256Pattern.MatchString(strings.TrimPrefix(dir, prefix)) {
		return "", fmt.Errorf("invalid selected Ubuntu release")
	}
	return strings.TrimPrefix(dir, prefix), nil
}
func ubuntuStart(c Config, s UbuntuSession, description, dir string) error {
	id, err := ubuntuRelease(c, dir)
	if err != nil {
		return err
	}
	if err = s.readyInputs(); err != nil {
		return err
	}
	if err = platformIdentity(c); err != nil {
		return err
	}
	var d Device
	if err = ReadJSON(c.DeviceFile, &d); err != nil {
		return err
	}
	var enrollment Enrollment
	if err = ReadJSON(c.EnrollmentFile, &enrollment); err != nil {
		return err
	}
	d.Enrollment = enrollment.EnrollmentID
	if d.Identity["platform_id"] != "ubuntu-host" {
		return fmt.Errorf("Ubuntu launcher cannot start another platform")
	}
	if _, err = c.Engine().installed(id, d); err != nil {
		return err
	}
	if err = trustedFile(filepath.Join(dir, "bin/lvglsim")); err != nil {
		return err
	}
	state, err := ownedUbuntuUnit(s, description)
	if err != nil {
		return err
	}
	if state["LoadState"] != "not-found" {
		return fmt.Errorf("Ubuntu session unit already exists")
	}
	// Staging is private until verified. Expose read/traverse access only for the
	// selected signed application, never write access or journal/credential files.
	for _, p := range []string{c.Root, filepath.Join(c.Root, "releases")} {
		if _, err = os.Stat(p); os.IsNotExist(err) {
			continue
		}
		if err = trustedFile(p); err != nil {
			return err
		}
		st, err := os.Stat(p)
		if err != nil || !st.IsDir() {
			return fmt.Errorf("invalid application root")
		}
		if err = os.Chmod(p, st.Mode().Perm()|0011); err != nil {
			return err
		}
	}
	if err = os.Chmod(dir, 0755); err != nil {
		return err
	}
	python, err := ubuntuProgram("/usr/bin/python3")
	if err != nil {
		return err
	}
	envProgram, err := ubuntuProgram("/usr/bin/env")
	if err != nil {
		return err
	}
	systemdRun, err := ubuntuProgram("/usr/bin/systemd-run")
	if err != nil {
		return err
	}
	env, err := s.environment()
	if err != nil {
		return err
	}
	args := []string{"--quiet", "--collect", "--unit=" + s.Unit, "--service-type=exec", "--property=Description=" + description, "--property=User=" + strconv.FormatUint(uint64(s.UID), 10), "--property=Group=" + strconv.FormatUint(uint64(s.GID), 10), "--property=KillMode=control-group", "--property=TimeoutStopSec=5s", "--property=NoNewPrivileges=yes", "--property=UMask=0077", "--property=Restart=no", "--property=StandardOutput=journal", "--property=StandardError=journal"}
	if s.LoginSession != "" {
		args = append(args, "--property=BindsTo=session-"+s.LoginSession+".scope", "--property=After=session-"+s.LoginSession+".scope")
	}
	args = append(args, envProgram, "-i")
	args = append(args, env...)
	args = append(args, python, "-I", s.Script, "run", "--release", dir, "--provider", s.Provider, "--runtime", s.Runtime, "--settings", s.Settings, "--screen", s.Screen)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, systemdRun, args...)
	cmd.Env = []string{"PATH=/usr/bin:/bin", "LANG=C"}
	if err = cmd.Run(); err != nil {
		return fmt.Errorf("systemd session startup failed: %w", err)
	}
	return nil
}
func ubuntuStop(s UbuntuSession, description string) error {
	state, err := ownedUbuntuUnit(s, description)
	if err != nil {
		return err
	}
	if state["LoadState"] == "not-found" {
		return nil
	}
	if _, err = ubuntuSystemctl("stop", s.Unit); err != nil {
		return err
	}
	state, err = ownedUbuntuUnit(s, description)
	if err != nil {
		return err
	}
	if state["LoadState"] != "not-found" && (state["ActiveState"] != "inactive" && state["ActiveState"] != "failed" || state["MainPID"] != "0") {
		return fmt.Errorf("Ubuntu unit did not stop")
	}
	return nil
}

type ubuntuProcess struct {
	PID        int    `json:"pid"`
	StartTicks uint64 `json:"start_ticks"`
	Exe        string `json:"exe"`
}
type ubuntuSessionState struct {
	Release    string        `json:"release"`
	Ready      bool          `json:"ready"`
	Supervisor ubuntuProcess `json:"supervisor"`
	Desktop    ubuntuProcess `json:"desktop"`
	Provider   ubuntuProcess `json:"provider"`
}

func userSessionRecord(s UbuntuSession) (ubuntuSessionState, error) {
	var record ubuntuSessionState
	if err := privateUserDirectory(s.Runtime, s.UID); err != nil {
		return record, err
	}
	f, err := os.OpenFile(filepath.Join(s.Runtime, "session.json"), os.O_RDONLY|syscall.O_NOFOLLOW, 0)
	if err != nil {
		return record, err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return record, err
	}
	sys, ok := st.Sys().(*syscall.Stat_t)
	if !ok || !st.Mode().IsRegular() || st.Mode().Perm() != 0600 || sys.Uid != s.UID || sys.Nlink != 1 {
		return record, fmt.Errorf("unsafe session record")
	}
	raw, err := io.ReadAll(io.LimitReader(f, 1<<20+1))
	if err != nil {
		return record, err
	}
	err = StrictJSON(raw, &record)
	return record, err
}
func checkUbuntuProcess(p ubuntuProcess, uid uint32, exe string, parent int) error {
	if p.PID <= 1 || p.Exe != exe {
		return fmt.Errorf("wrong session process identity")
	}
	root := fmt.Sprintf("/proc/%d", p.PID)
	st, err := os.Stat(root)
	if err != nil {
		return err
	}
	sys, ok := st.Sys().(*syscall.Stat_t)
	if !ok || sys.Uid != uid {
		return fmt.Errorf("wrong session process owner")
	}
	actual, err := os.Readlink(root + "/exe")
	if err != nil || actual != exe {
		return fmt.Errorf("session executable differs")
	}
	raw, err := os.ReadFile(root + "/stat")
	if err != nil {
		return err
	}
	i := strings.LastIndex(string(raw), ")")
	if i < 0 {
		return fmt.Errorf("invalid process stat")
	}
	fields := strings.Fields(string(raw)[i+1:])
	if len(fields) < 20 {
		return fmt.Errorf("incomplete process stat")
	}
	ticks, err := strconv.ParseUint(fields[19], 10, 64)
	if err != nil || ticks != p.StartTicks {
		return fmt.Errorf("session PID reused")
	}
	if parent > 0 && fields[1] != strconv.Itoa(parent) {
		return fmt.Errorf("session child does not belong to managed supervisor")
	}
	return nil
}
func ubuntuHealthOnce(s UbuntuSession, description, dir string) (UIHealth, error) {
	var h UIHealth
	if err := s.readyInputs(); err != nil {
		return h, err
	}
	state, err := ownedUbuntuUnit(s, description)
	if err != nil {
		return h, err
	}
	main, err := strconv.Atoi(state["MainPID"])
	if err != nil || main <= 1 || state["ActiveState"] != "active" {
		return h, fmt.Errorf("Ubuntu service is not active")
	}
	record, err := userSessionRecord(s)
	if err != nil {
		return h, err
	}
	if !record.Ready || record.Release != dir || record.Supervisor.PID != main {
		return h, fmt.Errorf("session is not ready for managed unit/release")
	}
	python, err := ubuntuProgram("/usr/bin/python3")
	if err != nil {
		return h, err
	}
	for _, p := range []struct {
		process ubuntuProcess
		exe     string
		parent  int
	}{{record.Supervisor, python, 0}, {record.Desktop, filepath.Join(dir, "bin/lvglsim"), main}, {record.Provider, s.Provider, main}} {
		if err = checkUbuntuProcess(p.process, s.UID, p.exe, p.parent); err != nil {
			return h, err
		}
	}
	env, err := s.environment()
	if err != nil {
		return h, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, python, "-I", s.Script, "health", "--release", dir, "--runtime", s.Runtime)
	cmd.Env = env
	cmd.SysProcAttr = &syscall.SysProcAttr{Credential: &syscall.Credential{Uid: s.UID, Gid: s.GID, Groups: []uint32{}}}
	raw, err := cmd.Output()
	if err != nil {
		return h, fmt.Errorf("actual desktop/Provider health failed")
	}
	if err = StrictJSON(raw, &h); err != nil {
		return h, err
	}
	now, err := uptimeMillis()
	if err != nil {
		return h, err
	}
	if err = validateUIHealth(h, now); err != nil {
		return h, err
	}
	if h.PID != record.Desktop.PID {
		return h, fmt.Errorf("health belongs to another UI")
	}
	return h, nil
}
func ubuntuOperation(c Config, args []string) error {
	if os.Geteuid() != 0 {
		return fmt.Errorf("trusted Ubuntu platform operations require root")
	}
	s, description, err := loadUbuntuSession(c)
	if err != nil {
		return err
	}
	switch args[0] {
	case "stop":
		if len(args) != 1 {
			return fmt.Errorf("unexpected stop arguments")
		}
		return ubuntuStop(s, description)
	case "start", "health":
		if len(args) != 2 {
			return fmt.Errorf("selected release required")
		}
		dir := args[1]
		if _, err = ubuntuRelease(c, dir); err != nil {
			return err
		}
		if args[0] == "start" {
			return ubuntuStart(c, s, description, dir)
		}
		last := uint64(0)
		count := 0
		deadline := time.Now().Add(50 * time.Second)
		for time.Now().Before(deadline) {
			h, e := ubuntuHealthOnce(s, description, dir)
			if e == nil && h.Tick > last {
				last = h.Tick
				count++
				if count >= 3 {
					return nil
				}
			} else if e != nil {
				count = 0
			}
			time.Sleep(time.Second)
		}
		return fmt.Errorf("managed Ubuntu session did not become healthy")
	case "idle":
		if len(args) != 1 {
			return fmt.Errorf("unexpected idle arguments")
		}
		state, e := c.Engine().readState()
		if e != nil {
			return e
		}
		h, e := ubuntuHealthOnce(s, description, c.Engine().dir(state.Current))
		if e != nil {
			return e
		}
		if h.Busy {
			return fmt.Errorf("Ubuntu desktop is busy")
		}
		return nil
	default:
		return fmt.Errorf("unsupported Ubuntu platform operation")
	}
}
