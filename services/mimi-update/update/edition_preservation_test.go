package update

// These exercise the real signed archive/filesystem transaction. Application
// start/health are simulated; this is not device or physical power-loss proof.
import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"syscall"
	"testing"
)

type preservedFile struct {
	hash     string
	mode     os.FileMode
	inode    uint64
	uid, gid uint32
	modified int64
}

func preservedSnapshot(t *testing.T, files []string) map[string]preservedFile {
	t.Helper()
	result := map[string]preservedFile{}
	for _, path := range files {
		info, err := os.Lstat(path)
		if err != nil || !info.Mode().IsRegular() {
			t.Fatalf("protected fixture missing or changed type: %s", path)
		}
		content, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		hash := sha256.Sum256(content)
		stat := info.Sys().(*syscall.Stat_t)
		result[path] = preservedFile{hex.EncodeToString(hash[:]), info.Mode(), stat.Ino, stat.Uid, stat.Gid, info.ModTime().UnixNano()}
	}
	return result
}

func editionFiles(t *testing.T, e *Engine, h *fakeHooks, edition string) ([]string, []string) {
	t.Helper()
	root := filepath.Join(filepath.Dir(e.Root), "rootfs")
	e.Root = filepath.Join(root, "overlay/smartdesktopmimi")
	e.Lock = filepath.Join(root, "var/lock/mimi-update.lock")
	if err := os.MkdirAll(filepath.Dir(e.Lock), 0755); err != nil {
		t.Fatal(err)
	}
	var paths []string
	put := func(relative string, content []byte, mode os.FileMode) {
		path := filepath.Join(root, relative)
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, content, mode); err != nil {
			t.Fatal(err)
		}
		paths = append(paths, path)
	}
	put("etc/smartdesktopmimi/product-edition", []byte(edition+"\n"), 0444)
	if edition == "private" {
		h.d.Capabilities = append(h.d.Capabilities, "tuya.device-pairing", "100ask.auto-pairing")
	}
	device, err := json.Marshal(h.d)
	if err != nil {
		t.Fatal(err)
	}
	put("etc/smartdesktopmimi/device.json", device, 0600)
	trust, err := e.Trust()
	if err != nil {
		t.Fatal(err)
	}
	trustBytes, err := json.Marshal(trust)
	if err != nil {
		t.Fatal(err)
	}
	put("etc/smartdesktopmimi/trust.json", trustBytes, 0644)
	put("overlay/network/state.json", []byte(`{"host_fixture":"preserve-network"}`), 0600)
	put("overlay/apps/notes.db", []byte("synthetic ordinary app data"), 0600)
	program := func(relative, input string) {
		content := []byte("host-only platform program fixture")
		if directory := os.Getenv("MIMI_EDITION_PROGRAM_FIXTURES"); directory != "" {
			var err error
			content, err = os.ReadFile(filepath.Join(directory, input))
			if err != nil {
				t.Fatal("configured real program fixture missing: ", err)
			}
		}
		put(relative, content, 0755)
	}
	program("usr/bin/lv_backend", "provider066-entry-"+edition+"/lv_backend")
	privatePaths := []string{"usr/bin/your_chat_bot_QIO_1.0.1.bin", "etc/100ask/device_sig",
		"etc/100ask/secret", "factory/tuya/license.env", "overlay/tuyadb/device-state.db"}
	var absent []string
	if edition == "private" {
		program(privatePaths[0], "tuya066-program-private/your_chat_bot_QIO_1.0.1.bin")
		for _, name := range privatePaths[1:] {
			put(name, []byte("synthetic host marker, not a credential\n"), 0600)
		}
	} else {
		for _, name := range privatePaths {
			absent = append(absent, filepath.Join(root, name))
		}
	}
	return paths, absent
}

func TestSharedSignedApplicationPreservesProductEditions(t *testing.T) {
	_, _, m, raw, sig, archive := engineFixture(t)
	for _, edition := range []string{"public", "private"} {
		for _, phase := range []string{"committed", "health-failure", "extracted", "staged", "journaled", "stopped", "started", "healthy"} {
			t.Run(edition+"/"+phase, func(t *testing.T) {
				e, h, _, _, _, _ := engineFixture(t)
				files, absent := editionFiles(t, e, h, edition)
				before := preservedSnapshot(t, files)
				h.healthFail = phase == "health-failure"
				if phase != "committed" && phase != "health-failure" {
					e.Fault = func(point string) error {
						if point == phase {
							return fmt.Errorf("host interruption at %s", point)
						}
						return nil
					}
				}
				err := e.Install(archive, raw, sig, "stable")
				if (phase == "committed") != (err == nil) {
					t.Fatalf("wrong transaction result at %s: %v", phase, err)
				}
				e.Fault = nil
				h.healthFail = false
				selected, err := e.Select()
				if err != nil {
					t.Fatal(err)
				}
				want := e.Factory
				if phase == "committed" {
					want = e.dir(m.PackageSHA256)
				}
				if selected != want {
					t.Fatalf("wrong selected release after %s", phase)
				}
				after := preservedSnapshot(t, files)
				for path, original := range before {
					if after[path] != original {
						t.Fatalf("protected file changed: %s", path)
					}
				}
				for _, path := range absent {
					if _, err := os.Lstat(path); !os.IsNotExist(err) {
						t.Fatalf("public update created private path: %s", path)
					}
				}
				state, err := e.Status()
				if err != nil || state.Trial {
					t.Fatal("unresolved transaction after recovery", err)
				}
				assertSharedEditionArchive(t, archive, m.PackageSHA256)
			})
		}
	}
	t.Logf("one signed package %s used for both editions; compiled-program fixtures=%t; simulated hooks only", m.PackageSHA256, os.Getenv("MIMI_EDITION_PROGRAM_FIXTURES") != "")
}

func assertSharedEditionArchive(t *testing.T, archive, expected string) {
	t.Helper()
	data, err := os.ReadFile(archive)
	if err != nil {
		t.Fatal(err)
	}
	h := sha256.Sum256(data)
	if hex.EncodeToString(h[:]) != expected {
		t.Fatal("shared signed archive changed")
	}
}

func TestSignedApplicationCannotReplacePrivatePlatformOrIdentity(t *testing.T) {
	for _, name := range []string{"usr/bin/lv_backend", "bin/your_chat_bot_QIO_1.0.1.bin", "etc/100ask/secret",
		"factory/tuya/license.env", "etc/smartdesktopmimi/device.json", "lib/../../etc/100ask/device_sig"} {
		t.Run(name, func(t *testing.T) {
			e, h, m, _, _, archive := engineFixture(t)
			files, _ := editionFiles(t, e, h, "private")
			before := preservedSnapshot(t, files)
			m.Files[name] = File{SHA256: m.Files["bin/lvglsim"].SHA256, Size: 1, Mode: 0644}
			m.ExpandedBytes++
			m.MaxFiles++
			raw, sig := sign(t, m)
			if err := e.Install(archive, raw, sig, "stable"); err == nil || h.stops != 0 {
				t.Fatal("forbidden signed payload reached application stop")
			}
			after := preservedSnapshot(t, files)
			for path, original := range before {
				if after[path] != original {
					t.Fatalf("rejected update changed protected file: %s", path)
				}
			}
		})
	}
}
