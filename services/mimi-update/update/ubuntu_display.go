package update

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
)

// A blank logind Display is permitted only through a fixed GNOME session
// process in the exact enrolled login scope. Never consult caller environment.
func ubuntuSessionProcessDisplay(proc string, uid uint32, session string) (string, error) {
	if !ubuntuLoginID.MatchString(session) {
		return "", fmt.Errorf("invalid login session")
	}
	return ubuntuSessionProcessDisplayWithExecutable(proc, uid, session, "/usr/libexec/gnome-session-binary")
}

func ubuntuSessionProcessDisplayWithExecutable(proc string, uid uint32, session, executable string) (string, error) {
	if err := trustedFile(executable); err != nil {
		return "", err
	}
	expected := fmt.Sprintf("/user.slice/user-%d.slice/session-%s.scope", uid, session)
	entries, err := os.ReadDir(proc)
	if err != nil {
		return "", err
	}
	displays := map[string]bool{}
	for _, entry := range entries {
		if _, err := strconv.ParseUint(entry.Name(), 10, 32); err != nil {
			continue
		}
		root := filepath.Join(proc, entry.Name())
		st, err := os.Stat(root)
		if err != nil {
			continue
		}
		owner, ok := st.Sys().(*syscall.Stat_t)
		if !ok || owner.Uid != uid {
			continue
		}
		exe, err := os.Readlink(filepath.Join(root, "exe"))
		if err != nil || exe != executable {
			continue
		}
		read := func(name string, limit int64) ([]byte, error) {
			f, err := os.Open(filepath.Join(root, name))
			if err != nil {
				return nil, err
			}
			defer f.Close()
			b, err := io.ReadAll(io.LimitReader(f, limit+1))
			if err == nil && int64(len(b)) > limit {
				err = fmt.Errorf("oversized session metadata")
			}
			return b, err
		}
		identity := func() (string, string, error) {
			st, err := os.Stat(root)
			if err != nil {
				return "", "", err
			}
			owner, ok := st.Sys().(*syscall.Stat_t)
			if !ok || owner.Uid != uid {
				return "", "", fmt.Errorf("session owner changed")
			}
			exe, err := os.Readlink(filepath.Join(root, "exe"))
			if err != nil || exe != executable {
				return "", "", fmt.Errorf("session executable changed")
			}
			raw, err := read("stat", 16384)
			if err != nil {
				return "", "", err
			}
			end := bytes.LastIndexByte(raw, ')')
			if end < 0 {
				return "", "", fmt.Errorf("invalid process stat")
			}
			fields := strings.Fields(string(raw[end+1:]))
			if len(fields) < 20 {
				return "", "", fmt.Errorf("missing process identity")
			}
			if _, err := strconv.ParseUint(fields[19], 10, 64); err != nil {
				return "", "", err
			}
			groups, err := read("cgroup", 65536)
			return fields[19], string(groups), err
		}
		start, groups, err := identity()
		if os.IsNotExist(err) {
			continue
		}
		if err != nil {
			return "", err
		}
		matches, differs := false, false
		for _, line := range strings.Split(groups, "\n") {
			fields := strings.SplitN(line, ":", 3)
			if len(fields) == 3 && (fields[1] == "" || fields[1] == "name=systemd") {
				matches = true
				if fields[2] != expected {
					differs = true
				}
			}
		}
		if !matches || differs {
			continue
		}
		raw, err := read("environ", 1048576)
		if os.IsNotExist(err) {
			continue
		}
		if err != nil {
			return "", err
		}
		env := map[string]string{}
		for _, entry := range strings.Split(string(raw), "\x00") {
			pair := strings.SplitN(entry, "=", 2)
			if pair[0] != "DISPLAY" && pair[0] != "XDG_SESSION_ID" && pair[0] != "XDG_SESSION_TYPE" {
				continue
			}
			if _, exists := env[pair[0]]; exists || len(pair) != 2 {
				return "", fmt.Errorf("ambiguous session environment")
			}
			env[pair[0]] = pair[1]
		}
		if env["XDG_SESSION_ID"] != session || env["XDG_SESSION_TYPE"] != "x11" || !ubuntuDisplay.MatchString(env["DISPLAY"]) {
			return "", fmt.Errorf("session display metadata differs")
		}
		after, afterGroups, err := identity()
		if os.IsNotExist(err) {
			continue
		}
		if err != nil {
			return "", err
		}
		if start != after || groups != afterGroups {
			return "", fmt.Errorf("session process changed")
		}
		displays[env["DISPLAY"]] = true
	}
	if len(displays) != 1 {
		return "", fmt.Errorf("no unique session display")
	}
	for display := range displays {
		return display, nil
	}
	panic("unreachable")
}
