package update

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestSystemJournalRequiresPersistedFields(t *testing.T) {
	dir := t.TempDir()
	s, err := systemJournal(dir)
	if err != nil || s.Phase != "idle" || s.Schema != 1 {
		t.Fatalf("absent: %+v %v", s, err)
	}
	valid := SystemJournal{Schema: 1, HighWater: 42, Phase: "failed", Version: "1.2.3", PackageSHA256: "fixture"}
	if err = atomicJSON(dir, "state.json", valid); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(filepath.Join(dir, "state.json"))
	if err != nil {
		t.Fatal(err)
	}
	for _, key := range []string{"schema_version", "high_water", "phase", "version", "package_sha256", "boot_id", "product_edition"} {
		for _, null := range []bool{false, true} {
			if !null && (key == "boot_id" || key == "product_edition") {
				continue
			}
			name := "missing-" + key
			if null {
				name = "null-" + key
			}
			t.Run(name, func(t *testing.T) {
				var fields map[string]json.RawMessage
				json.Unmarshal(raw, &fields)
				if null {
					fields[key] = json.RawMessage("null")
				} else {
					delete(fields, key)
				}
				data, _ := json.Marshal(fields)
				if err := os.WriteFile(filepath.Join(dir, "state.json"), data, 0600); err != nil {
					t.Fatal(err)
				}
				if _, err := systemJournal(dir); err == nil {
					t.Fatal("incomplete/null systemjournal accepted")
				}
				after, _ := os.ReadFile(filepath.Join(dir, "state.json"))
				if string(after) != string(data) {
					t.Fatal("failed read mutated journal")
				}
			})
		}
	}
	if err = atomicJSON(dir, "state.json", valid); err != nil {
		t.Fatal(err)
	}
	got, err := systemJournal(dir)
	if err != nil || got.HighWater != 42 || got.Phase != "failed" {
		t.Fatalf("roundtrip: %+v %v", got, err)
	}
}

func TestIncompleteSystemJournalBlocksApplicationInstall(t *testing.T) {
	e, h, m, raw, sig, archive := engineFixture(t)
	dir := filepath.Join(e.Root, "system")
	if err := os.MkdirAll(dir, 0700); err != nil {
		t.Fatal(err)
	}
	bad := []byte(`{}`)
	path := filepath.Join(dir, "state.json")
	if err := os.WriteFile(path, bad, 0600); err != nil {
		t.Fatal(err)
	}
	if err := e.Install(archive, raw, sig, "stable"); err == nil {
		t.Fatal("application installed with incomplete system journal")
	}
	if len(h.starts) != 0 {
		t.Fatal("application started")
	}
	if _, err := os.Stat(e.dir(m.PackageSHA256)); !os.IsNotExist(err) {
		t.Fatal("candidate staged despite invalid journal")
	}
	after, _ := os.ReadFile(path)
	if string(after) != string(bad) {
		t.Fatal("invalid journal altered")
	}
}
