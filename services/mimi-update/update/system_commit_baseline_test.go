package update

import (
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestSystemCommitRequiresDestinationBaseline(t *testing.T) {
	for _, phase := range []string{"applying", "pending-reboot"} {
		for _, baseline := range []string{"", "different-build", "target-build"} {
			t.Run(phase+"/"+baseline, func(t *testing.T) {
				var s SystemJournal
				raw := `{"schema_version":1,"phase":"` + phase + `","version":"1.1.0","system_baseline":"` + baseline + `"}`
				if err := json.Unmarshal([]byte(raw), &s); err != nil {
					t.Fatal(err)
				}
				d := Device{Runtime: map[string]string{"system": "1.1.0"}, SystemBaseline: "target-build"}
				err := validateSystemCommit(s, d)
				if baseline == "target-build" {
					if err != nil {
						t.Fatal(err)
					}
				} else if err == nil {
					t.Fatal("rollback cancellation admitted without matching destination baseline")
				}
			})
		}
	}
}

func TestSystemRecoveryRequiresDestinationBaseline(t *testing.T) {
	for _, phase := range []string{"applying", "pending-reboot"} {
		for _, baseline := range []string{"", "different-build", "target-build"} {
			t.Run(phase+"/"+baseline, func(t *testing.T) {
				e := &Engine{Root: t.TempDir()}
				dir := filepath.Join(e.Root, "system")
				s := SystemJournal{Schema: 1, Phase: phase, Version: "1.1.0", HighWater: 42, PackageSHA256: "preserved", BootID: "previous-boot", SystemBaseline: baseline}
				if err := atomicJSON(dir, "state.json", s); err != nil {
					t.Fatal(err)
				}
				path := filepath.Join(dir, "state.json")
				before, err := os.ReadFile(path)
				if err != nil {
					t.Fatal(err)
				}
				got, err := e.reconcileSystem(Device{Runtime: map[string]string{"system": "1.1.0"}, SystemBaseline: "target-build"})
				if baseline == "" {
					if err == nil {
						t.Fatal("legacy pending journal inferred identity")
					}
					after, readErr := os.ReadFile(path)
					if readErr != nil || string(before) != string(after) {
						t.Fatal("legacy bytes modified", readErr)
					}
					return
				}
				want := s
				if baseline != "target-build" {
					want.Phase = "failed"
				}
				if err != nil || !reflect.DeepEqual(got, want) {
					t.Fatal("wrong recovery", got, err)
				}
				saved, err := systemJournal(dir)
				if err != nil || !reflect.DeepEqual(saved, want) {
					t.Fatal("wrong persisted recovery", saved, err)
				}
			})
		}
	}
}
