package update

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestJournalRequiresPersistedFields(t *testing.T) {
	e := &Engine{Root: t.TempDir()}
	initial, err := e.readState()
	if err != nil || initial.Current != "factory" || initial.Schema != 1 {
		t.Fatalf("absent journal: %+v %v", initial, err)
	}
	valid := State{Schema: 1, Current: "factory", HighWater: 42}
	if err = e.writeState(valid); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(filepath.Join(e.Root, "state.json"))
	if err != nil {
		t.Fatal(err)
	}
	var fields map[string]json.RawMessage
	if err = json.Unmarshal(raw, &fields); err != nil {
		t.Fatal(err)
	}
	for _, key := range []string{"schema_version", "current", "previous", "trial", "high_water", "failed", "history"} {
		t.Run("missing-"+key, func(t *testing.T) {
			var altered map[string]json.RawMessage
			json.Unmarshal(raw, &altered)
			delete(altered, key)
			data, _ := json.Marshal(altered)
			os.WriteFile(filepath.Join(e.Root, "state.json"), data, 0600)
			if _, err := e.readState(); err == nil {
				t.Fatal("incomplete persisted journal accepted")
			}
			after, _ := os.ReadFile(filepath.Join(e.Root, "state.json"))
			if string(after) != string(data) {
				t.Fatal("read mutated failed journal")
			}
		})
	}
	for _, key := range []string{"schema_version", "current", "previous", "trial", "high_water"} {
		t.Run("null-"+key, func(t *testing.T) {
			var altered map[string]json.RawMessage
			json.Unmarshal(raw, &altered)
			altered[key] = json.RawMessage("null")
			data, _ := json.Marshal(altered)
			os.WriteFile(filepath.Join(e.Root, "state.json"), data, 0600)
			if _, err := e.readState(); err == nil {
				t.Fatal("null scalar accepted")
			}
		})
	}
	if err = e.writeState(valid); err != nil {
		t.Fatal(err)
	}
	got, err := e.readState()
	if err != nil || got.HighWater != 42 {
		t.Fatalf("writer roundtrip: %+v %v", got, err)
	}
}
