package update

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func sessionFixture() UbuntuSession {
	return UbuntuSession{Schema: 1, UID: 1000, GID: 1000, Unit: "mimi-ubuntu-acceptance.service", Script: "/usr/lib/mimi/session.py", ScriptSHA256: strings.Repeat("a", 64), Provider: "/usr/lib/mimi/provider", ProviderSHA256: strings.Repeat("b", 64), Runtime: "/run/mimi/user", Settings: "/var/lib/mimi-data/user/settings.json", Screen: "1024x768", Display: ":110", XAuthority: "/run/mimi/display.auth"}
}
func TestUbuntuSessionRejectsScopeEscapes(t *testing.T) {
	s := sessionFixture()
	if e := s.validate(); e != nil {
		t.Fatal(e)
	}
	for name, mutate := range map[string]func(*UbuntuSession){"root": func(x *UbuntuSession) { x.UID = 0 }, "root group": func(x *UbuntuSession) { x.GID = 0 }, "unrelated unit": func(x *UbuntuSession) { x.Unit = "ssh.service" }, "unit args": func(x *UbuntuSession) { x.Unit = "mimi-ubuntu-test.service --all" }, "remote display": func(x *UbuntuSession) { x.Display = "host:0" }, "traversal": func(x *UbuntuSession) { x.Script = "/usr/lib/../tmp/session.py" }, "control character": func(x *UbuntuSession) { x.Provider = "/usr/lib/mimi/\nprovider" }, "missing hash": func(x *UbuntuSession) { x.ScriptSHA256 = "" }, "settings overlap": func(x *UbuntuSession) { x.Settings = x.Runtime + "/settings.json" }, "long socket": func(x *UbuntuSession) { x.Runtime = "/run/" + strings.Repeat("a", 100) }, "wrong screen": func(x *UbuntuSession) { x.Screen = "1024x768;id" }, "same component": func(x *UbuntuSession) { x.Provider = x.Script }} {
		t.Run(name, func(t *testing.T) {
			x := s
			mutate(&x)
			if e := x.validate(); e == nil {
				t.Fatal("unsafe session accepted")
			}
		})
	}
}
func TestUbuntuReleaseMustBeSelectedHashOrFactory(t *testing.T) {
	c := Config{Root: "/var/lib/mimi", Factory: "/usr/lib/mimi/factory"}
	for _, p := range []string{c.Factory, c.Root + "/releases/" + strings.Repeat("a", 64)} {
		if _, e := ubuntuRelease(c, p); e != nil {
			t.Fatal(e)
		}
	}
	for _, p := range []string{"/tmp/app", c.Root + "/releases/../factory", c.Factory + "/", c.Root + "/releases/" + strings.Repeat("a", 64) + "/bin", c.Root + "/releases/ABC"} {
		if _, e := ubuntuRelease(c, p); e == nil {
			t.Fatal("unsafe release accepted")
		}
	}
}
func TestUbuntuServiceProjectionRejectsDuplicateKeys(t *testing.T) {
	if _, e := unitProperties([]byte("MainPID=1\nMainPID=2\n")); e == nil {
		t.Fatal("ambiguous state accepted")
	}
	if _, e := unitProperties([]byte("MainPID")); e == nil {
		t.Fatal("malformed state accepted")
	}
	s, e := unitProperties([]byte("LoadState=not-found\nMainPID=0\n"))
	if e != nil || s["LoadState"] != "not-found" {
		t.Fatal(e)
	}
}
func TestUbuntuProcessMustMatchRealOwnerStartAndParent(t *testing.T) {
	pid := os.Getpid()
	root := "/proc/" + strconv.Itoa(pid)
	raw, e := os.ReadFile(root + "/stat")
	if e != nil {
		t.Fatal(e)
	}
	fields := strings.Fields(string(raw)[strings.LastIndex(string(raw), ")")+1:])
	ticks, _ := strconv.ParseUint(fields[19], 10, 64)
	exe, e := os.Readlink(root + "/exe")
	if e != nil {
		t.Fatal(e)
	}
	p := ubuntuProcess{PID: pid, StartTicks: ticks, Exe: exe}
	if e = checkUbuntuProcess(p, uint32(os.Getuid()), exe, os.Getppid()); e != nil {
		t.Fatal(e)
	}
	cases := []struct {
		p      ubuntuProcess
		uid    uint32
		exe    string
		parent int
	}{{p, uint32(os.Getuid() + 1), exe, os.Getppid()}, {p, uint32(os.Getuid()), exe, 999999}, {ubuntuProcess{pid, ticks + 1, exe}, uint32(os.Getuid()), exe, os.Getppid()}, {p, uint32(os.Getuid()), filepath.Join("/tmp", "different"), os.Getppid()}}
	for _, c := range cases {
		if e = checkUbuntuProcess(c.p, c.uid, c.exe, c.parent); e == nil {
			t.Fatal("different real process accepted")
		}
	}
}

func TestUbuntuGraphicalLoginScope(t *testing.T) {
	s := sessionFixture()
	s.LoginSession = "42"
	values := map[string]string{"Id": "42", "User": "1000", "Class": "user", "Type": "x11", "Remote": "no", "Active": "yes", "Seat": "seat0", "Display": ":110", "Scope": "session-42.scope"}
	if err := s.validate(); err != nil {
		t.Fatal(err)
	}
	if err := checkUbuntuLoginProperties(s, values); err != nil {
		t.Fatal(err)
	}
	for key, bad := range map[string]string{"Id": "43", "User": "125", "Class": "greeter", "Type": "wayland", "Remote": "yes", "Active": "no", "Seat": "seat1", "Display": ":0", "Scope": "session-c1.scope"} {
		t.Run(key, func(t *testing.T) {
			old := values[key]
			values[key] = bad
			defer func() { values[key] = old }()
			if checkUbuntuLoginProperties(s, values) == nil {
				t.Fatal("foreign or inactive login accepted")
			}
		})
	}
	for _, bad := range []string{"../1", "1 --all", "1.scope", "", "1\n"} {
		if bad == "" {
			continue
		}
		s.LoginSession = bad
		if s.validate() == nil {
			t.Fatal("invalid login ID")
		}
	}
	s.LoginSession = ""
	if err := s.validate(); err != nil {
		t.Fatal("legacy explicit session changed", err)
	}
}
