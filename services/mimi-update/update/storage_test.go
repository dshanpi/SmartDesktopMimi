package update

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func storageFixture(t *testing.T) (string, func(string, string)) {
	t.Helper()
	root := t.TempDir()
	write := func(p, s string) {
		t.Helper()
		p = filepath.Join(root, p)
		if err := os.MkdirAll(filepath.Dir(p), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(s), 0644); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.MkdirAll(filepath.Join(root, "overlay"), 0755); err != nil {
		t.Fatal(err)
	}
	write("proc/self/mountinfo", "1 0 179:4 / / rw,relatime - ext4 /dev/root rw\n2 1 179:7 / /overlay rw - ext4 /dev/mmcblk0p7 rw\n")
	write("sys/dev/block/179:4/uevent", "MAJOR=179\nMINOR=4\nDEVNAME=mmcblk0p4\n")
	write("sys/dev/block/179:7/uevent", "DEVNAME=mmcblk0p7\n")
	write("sys/fs/ext4/mmcblk0p4/errors_count", "0\n")
	write("sys/fs/ext4/mmcblk0p7/errors_count", "0\n")
	return root, write
}
func TestStoragePreflight(t *testing.T) {
	cases := []struct {
		name   string
		change func(string, func(string, string))
		want   string
	}{
		{"clean", func(string, func(string, string)) {}, ""},
		{"root-errors", func(_ string, w func(string, string)) { w("sys/fs/ext4/mmcblk0p4/errors_count", "1\n") }, "EXT4 has 1 error"},
		{"data-errors", func(_ string, w func(string, string)) { w("sys/fs/ext4/mmcblk0p7/errors_count", "4\n") }, "EXT4 has 4 error"},
		{"unknown-counter", func(r string, _ func(string, string)) {
			os.Remove(filepath.Join(r, "sys/fs/ext4/mmcblk0p7/errors_count"))
		}, "counter unavailable"},
		{"invalid-counter", func(_ string, w func(string, string)) { w("sys/fs/ext4/mmcblk0p4/errors_count", "-1") }, "invalid EXT4"},
		{"overflow-counter", func(_ string, w func(string, string)) {
			w("sys/fs/ext4/mmcblk0p4/errors_count", "18446744073709551616")
		}, "invalid EXT4"},
		{"empty-counter", func(_ string, w func(string, string)) { w("sys/fs/ext4/mmcblk0p4/errors_count", "") }, "invalid EXT4"},
		{"missing-device", func(_ string, w func(string, string)) { w("sys/dev/block/179:4/uevent", "MAJOR=179\n") }, "invalid block device name"},
		{"duplicate-device", func(_ string, w func(string, string)) {
			w("sys/dev/block/179:4/uevent", "DEVNAME=mmcblk0p4\nDEVNAME=mmcblk0p7\n")
		}, "ambiguous"},
		{"device-traversal", func(_ string, w func(string, string)) { w("sys/dev/block/179:4/uevent", "DEVNAME=../../tmp\n") }, "invalid block device name"},
		{"root-readonly", func(_ string, w func(string, string)) {
			w("proc/self/mountinfo", "1 0 179:4 / / ro - ext4 /dev/root ro\n")
		}, "read-only"},
		{"superblock-readonly", func(_ string, w func(string, string)) {
			w("proc/self/mountinfo", "1 0 179:4 / / rw - ext4 /dev/root ro\n")
		}, "read-only"},
		{"bind-readonly", func(_ string, w func(string, string)) {
			w("proc/self/mountinfo", "1 0 179:4 / / rw - ext4 /dev/root rw\n2 1 179:4 /data /overlay ro - ext4 /dev/root rw\n")
		}, "read-only"},
		{"unsupported-overlay", func(_ string, w func(string, string)) {
			w("proc/self/mountinfo", "1 0 0:1 / / rw - overlay overlay rw\n")
		}, "unsupported storage"},
		{"bad-mountinfo", func(_ string, w func(string, string)) { w("proc/self/mountinfo", "garbage\n") }, "malformed mountinfo"},
		{"empty-mountinfo", func(_ string, w func(string, string)) { w("proc/self/mountinfo", "") }, "mountinfo is empty"},
		{"missing-mountinfo", func(r string, _ func(string, string)) { os.Remove(filepath.Join(r, "proc/self/mountinfo")) }, "storage preflight"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			r, w := storageFixture(t)
			tc.change(r, w)
			report, err := inspectStorage(r, "/overlay/mimi")
			if tc.want == "" {
				if err != nil || !report.Passed {
					t.Fatalf("%+v %v", report, err)
				}
				if report.Volumes[0].Device != "mmcblk0p4" || report.Volumes[1].Device != "mmcblk0p7" {
					t.Fatal(report)
				}
			} else if err == nil || report.Passed || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("wanted %s: %+v %v", tc.want, report, err)
			}
			if _, err := os.Stat(filepath.Join(r, "overlay/mimi")); !os.IsNotExist(err) {
				t.Fatal("diagnostic created data directory")
			}
		})
	}
}
func TestStorageResolvedMountAndEscapes(t *testing.T) {
	r, w := storageFixture(t)
	if err := os.MkdirAll(filepath.Join(r, "data disk"), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("data disk", filepath.Join(r, "persistent")); err != nil {
		t.Fatal(err)
	}
	w("proc/self/mountinfo", "1 0 179:4 / / rw shared:1 - ext4 /dev/root rw\n2 1 179:7 / /data\\040disk rw - ext4 /dev/mmcblk0p7 rw\n")
	report, err := inspectStorage(r, "/persistent/new")
	if err != nil || report.Volumes[1].Mount != "/data disk" {
		t.Fatalf("%+v %v", report, err)
	}
	// A sibling name must not match a mount by mere string prefix.
	if err := os.MkdirAll(filepath.Join(r, "data disk2"), 0755); err != nil {
		t.Fatal(err)
	}
	report, err = inspectStorage(r, "/data disk2/new")
	if err != nil || report.Volumes[1].Mount != "/" {
		t.Fatalf("%+v %v", report, err)
	}
}

type storageFailHooks struct {
	*fakeHooks
	calls, failAt int
}

func (h *storageFailHooks) Preflight() error {
	h.calls++
	if h.calls == h.failAt {
		return fmt.Errorf("storage preflight: EXT4 errors")
	}
	return nil
}

func TestStorageFailureBlocksApplicationTransaction(t *testing.T) {
	for _, at := range []int{1, 2, 3} {
		t.Run(fmt.Sprint(at), func(t *testing.T) {
			e, h, _, raw, sig, p := engineFixture(t)
			g := &storageFailHooks{fakeHooks: h, failAt: at}
			e.Hooks = g
			err := e.Install(p, raw, sig, "stable")
			if err == nil || !strings.Contains(err.Error(), "storage preflight") {
				t.Fatal(err)
			}
			s, err := e.Status()
			if err != nil {
				t.Fatal(err)
			}
			if at < 3 {
				if s.Current != "factory" || s.Trial || h.stops != 0 || len(h.starts) != 0 {
					t.Fatalf("rejected update changed desktop: %+v %+v", s, h)
				}
				if _, err = os.Stat(filepath.Join(e.Root, "state.json")); !os.IsNotExist(err) {
					t.Fatal("rejected update wrote journal")
				}
			} else if !s.Trial {
				t.Fatal("storage fault incorrectly committed trial")
			}
			if at == 1 {
				if _, err = os.Stat(e.Root); !os.IsNotExist(err) {
					t.Fatal("early rejection wrote data")
				}
			}
		})
	}
}
func TestStorageFailureBlocksSystemConfirmation(t *testing.T) {
	for _, at := range []int{1, 2} {
		t.Run(fmt.Sprint(at), func(t *testing.T) {
			e, h, _, _, _, _ := engineFixture(t)
			e.Hooks = &storageFailHooks{fakeHooks: h, failAt: at}
			called := false
			err := e.SystemHealth(func() error { called = true; return nil })
			if err == nil || !strings.Contains(err.Error(), "storage preflight") || called {
				t.Fatalf("unsafe system confirmation: %v called=%v", err, called)
			}
		})
	}
}
