package update

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestUbuntuDisplayBoundToGNOMESession(t *testing.T) {
	root := t.TempDir()
	uid := uint32(os.Getuid())
	fixture := func(pid, display string) string {
		p := filepath.Join(root, pid)
		if err := os.Mkdir(p, 0700); err != nil {
			t.Fatal(err)
		}
		if err := os.Symlink("/usr/bin/true", filepath.Join(p, "exe")); err != nil {
			t.Fatal(err)
		}
		for name, value := range map[string]string{"stat": pid + " (gnome-session-b) S " + strings.Repeat("0 ", 18) + "1234", "cgroup": fmt.Sprintf("0::/user.slice/user-%d.slice/session-2.scope\n", uid), "environ": "DISPLAY=" + display + "\x00XDG_SESSION_ID=2\x00XDG_SESSION_TYPE=x11\x00"} {
			if err := os.WriteFile(filepath.Join(p, name), []byte(value), 0600); err != nil {
				t.Fatal(err)
			}
		}
		return p
	}
	p := fixture("42", ":7")
	check := func(want bool) {
		t.Helper()
		got, err := ubuntuSessionProcessDisplayWithExecutable(root, uid, "2", "/usr/bin/true")
		if want && (err != nil || got != ":7") {
			t.Fatalf("%q %v", got, err)
		}
		if !want && err == nil {
			t.Fatal("unsafe display accepted")
		}
	}
	check(true)
	good, _ := os.ReadFile(filepath.Join(p, "environ"))
	for name, raw := range map[string]string{"missing": "", "remote": strings.ReplaceAll(string(good), ":7", "host:7"), "other-session": strings.ReplaceAll(string(good), "ID=2", "ID=3"), "wayland": strings.ReplaceAll(string(good), "x11", "wayland"), "duplicate": string(good) + "DISPLAY=:7\x00", "huge": strings.Repeat("x", 1048577)} {
		t.Run(name, func(t *testing.T) { os.WriteFile(filepath.Join(p, "environ"), []byte(raw), 0600); check(false) })
	}
	os.WriteFile(filepath.Join(p, "environ"), good, 0600)
	for _, scope := range []string{"session-20.scope", "session-2.scope/child", "session-3.scope"} {
		os.WriteFile(filepath.Join(p, "cgroup"), []byte(fmt.Sprintf("0::/user.slice/user-%d.slice/%s\n", uid, scope)), 0600)
		check(false)
	}
	os.WriteFile(filepath.Join(p, "cgroup"), []byte(fmt.Sprintf("1:name=systemd:/user.slice/user-%d.slice/session-2.scope\n", uid)), 0600)
	check(true)
	if _, err := ubuntuSessionProcessDisplayWithExecutable(root, uid+1, "2", "/usr/bin/true"); err == nil {
		t.Fatal("wrong uid accepted")
	}
	os.Remove(filepath.Join(p, "exe"))
	os.Symlink("/usr/bin/sleep", filepath.Join(p, "exe"))
	check(false)
	os.Remove(filepath.Join(p, "exe"))
	os.Symlink("/usr/bin/true", filepath.Join(p, "exe"))
	fixture("43", ":7")
	check(true)
	fixture("44", ":8")
	check(false)
}

func TestUbuntuDisplayActualLoginReadOnly(t *testing.T) {
	sid := os.Getenv("MIMI_TEST_REAL_LOGIN_SESSION")
	if sid == "" {
		t.Skip("explicit real login session not supplied")
	}
	display, err := ubuntuSessionProcessDisplay("/proc", uint32(os.Getuid()), sid)
	if err != nil || display != os.Getenv("MIMI_TEST_REAL_LOGIN_DISPLAY") {
		t.Fatalf("actual session display not verified: %q %v", display, err)
	}
}

func TestUbuntuActualLoginLauncherRecheck(t *testing.T) {
	sid := os.Getenv("MIMI_TEST_REAL_LOGIN_SESSION")
	if sid == "" {
		t.Skip("explicit real login session not supplied")
	}
	s := sessionFixture()
	s.UID = uint32(os.Getuid())
	s.LoginSession = sid
	s.Display = os.Getenv("MIMI_TEST_REAL_LOGIN_DISPLAY")
	if err := s.readyLogin(); err != nil {
		t.Fatal(err)
	}
	s.Display = ":9999"
	if err := s.readyLogin(); err == nil {
		t.Fatal("wrong display accepted")
	}
}
