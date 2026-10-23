package update

import (
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestSystemRecoveryJournalMatrix(t *testing.T) {
	boot, err := os.ReadFile("/proc/sys/kernel/random/boot_id")
	if err != nil {
		t.Fatal(err)
	}
	live := strings.TrimSpace(string(boot))
	for _, phase := range []string{"applying", "pending-reboot"} {
		for _, changed := range []bool{false, true} {
			for _, matches := range []bool{false, true} {
				name := phase
				if changed {
					name += "/changed-boot"
				} else {
					name += "/same-boot"
				}
				if matches {
					name += "/target-runtime"
				} else {
					name += "/old-runtime"
				}
				t.Run(name, func(t *testing.T) {
					e := &Engine{Root: t.TempDir()}
					dir := filepath.Join(e.Root, "system")
					s := SystemJournal{SystemBaseline: "target-build", Schema: 1, Phase: phase, HighWater: 42, Version: "2.0.0", PackageSHA256: strings.Repeat("a", 64), BootID: live, ProductEdition: "private"}
					if changed {
						s.BootID = "previous-boot"
					}
					if err := atomicJSON(dir, "state.json", s); err != nil {
						t.Fatal(err)
					}
					path := filepath.Join(dir, "state.json")
					before, err := os.ReadFile(path)
					if err != nil {
						t.Fatal(err)
					}
					d := Device{SystemBaseline: "target-build", Runtime: map[string]string{"system": "1.0.0"}}
					if matches {
						d.Runtime["system"] = s.Version
					}
					expected := s
					if changed && !matches {
						expected.Phase = "failed"
					}
					got, err := e.reconcileSystem(d)
					if err != nil || !reflect.DeepEqual(got, expected) {
						t.Fatalf("got=%+v err=%v want=%+v", got, err, expected)
					}
					persisted, err := systemJournal(dir)
					if err != nil || !reflect.DeepEqual(persisted, expected) {
						t.Fatalf("persisted=%+v err=%v", persisted, err)
					}
					after, err := os.ReadFile(path)
					if err != nil {
						t.Fatal(err)
					}
					if expected.Phase == phase && string(before) != string(after) {
						t.Fatal("unchanged transaction rewritten")
					}
					again, err := e.reconcileSystem(d)
					final, readErr := os.ReadFile(path)
					if err != nil || readErr != nil || !reflect.DeepEqual(again, expected) || string(final) != string(after) {
						t.Fatal("recovery not idempotent", err, readErr)
					}
				})
			}
		}
		t.Run(phase+"/missing-boot", func(t *testing.T) {
			e := &Engine{Root: t.TempDir()}
			dir := filepath.Join(e.Root, "system")
			s := SystemJournal{SystemBaseline: "target-build", Schema: 1, Phase: phase, HighWater: 42, Version: "2.0.0", PackageSHA256: strings.Repeat("a", 64)}
			if err := atomicJSON(dir, "state.json", s); err != nil {
				t.Fatal(err)
			}
			path := filepath.Join(dir, "state.json")
			before, _ := os.ReadFile(path)
			if _, err := e.reconcileSystem(Device{SystemBaseline: "target-build", Runtime: map[string]string{"system": "1.0.0"}}); err == nil {
				t.Fatal("missing boot identity admitted")
			}
			after, err := os.ReadFile(path)
			if err != nil || string(before) != string(after) {
				t.Fatal("rejection changed journal", err)
			}
		})
	}
}

func TestRecoveredSystemJournalApplicationAdmission(t *testing.T) {
	boot, err := os.ReadFile("/proc/sys/kernel/random/boot_id")
	if err != nil {
		t.Fatal(err)
	}
	for _, phase := range []string{"applying", "pending-reboot"} {
		for _, mode := range []string{"same-boot", "target-boot", "old-system-after-boot"} {
			t.Run(phase+"/"+mode, func(t *testing.T) {
				e, h, m, raw, sig, archive := engineFixture(t)
				s := SystemJournal{SystemBaseline: h.d.SystemBaseline, Schema: 1, Phase: phase, HighWater: 42, Version: h.d.Runtime["system"], PackageSHA256: strings.Repeat("a", 64), BootID: strings.TrimSpace(string(boot))}
				if mode != "same-boot" {
					s.BootID = "previous-boot"
				}
				if mode == "old-system-after-boot" {
					s.Version = "99.0.0"
				}
				dir := filepath.Join(e.Root, "system")
				if err := atomicJSON(dir, "state.json", s); err != nil {
					t.Fatal(err)
				}
				err := e.Install(archive, raw, sig, m.Channel)
				if mode == "old-system-after-boot" {
					if err != nil || len(h.starts) != 1 {
						t.Fatal("failed system transaction should permit compatible app install", err, h.starts)
					}
					s.Phase = "failed"
				} else {
					if err == nil || !strings.Contains(err.Error(), "pending system") || len(h.starts) != 0 || h.stops != 0 {
						t.Fatal("pending system fence violated", err, h.starts, h.stops)
					}
					if _, err := os.Stat(e.dir(m.PackageSHA256)); !os.IsNotExist(err) {
						t.Fatal("pending transaction staged app", err)
					}
				}
				got, err := systemJournal(dir)
				if err != nil || !reflect.DeepEqual(got, s) {
					t.Fatal("app admission changed system replay/transaction metadata", got, err)
				}
			})
		}
	}
}
