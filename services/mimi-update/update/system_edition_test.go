package update

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

// All signatures/archives are generated host fixtures; no SWUpdate or hardware calls.
func TestSignedSystemProductEditionMatrix(t *testing.T) {
	c, d, trust, _ := systemFixture(t)
	var m SystemManifest
	if err := StrictJSON(c.Raw, &m); err != nil {
		t.Fatal(err)
	}
	for _, installed := range []string{"", "public", "private", "unknown"} {
		for _, candidate := range []string{"", "public", "private", "unknown"} {
			t.Run(installed+"-to-"+candidate, func(t *testing.T) {
				d.ProductEdition = installed
				m.ProductEdition = candidate
				c.Raw, c.Signature = signSystem(t, m)
				_, err := c.Verify(d, trust, "stable", 0)
				want := installed == candidate && installed != "unknown"
				if (err == nil) != want {
					t.Fatalf("edition admission: want %v, err=%v", want, err)
				}
			})
		}
	}
	d.ProductEdition = "public"
	m.ProductEdition = "public"
	c.Raw, c.Signature = signSystem(t, m)
	m.ProductEdition = "private"
	changed, _ := signSystem(t, m)
	c.Raw = changed
	if _, err := c.Verify(d, trust, "stable", 0); err == nil {
		t.Fatal("edition tampering bypassed signature")
	}
}

func TestSystemProductCandidateSelectionAndDownloadFence(t *testing.T) {
	c, d, trust, payload := systemFixture(t)
	d.ProductEdition = "public"
	assets := map[string][]byte{}
	var releases []Release
	for i, edition := range []string{"public", "private"} {
		item := clone(c)
		var m SystemManifest
		if err := StrictJSON(item.Raw, &m); err != nil {
			t.Fatal(err)
		}
		m.ProductEdition = edition
		tag := "system-a133-v1.1.0"
		if i == 1 {
			m.Version = "1.2.0"
			m.OTA.Device.Runtime["system"] = m.Version
			tag = "system-a133-v1.2.0"
		}
		item.Raw, item.Signature = signSystem(t, m)
		appendRelease(&releases, assets, tag, false, false, map[string][]byte{
			"system-manifest.json": item.Raw, "system-manifest.sig": item.Signature,
			"factory-manifest.json": item.FactoryRaw, "factory-manifest.sig": item.FactorySignature, "system.swu": payload,
		})
		if edition == "private" {
			dir := filepath.Join(t.TempDir(), "must-not-create")
			if _, err := testGitHub(t, nil, nil).DownloadSystem(context.Background(), item, d, trust, "stable", 0, dir); err == nil {
				t.Fatal("wrong edition downloaded")
			}
			if _, err := os.Stat(dir); !os.IsNotExist(err) {
				t.Fatal("wrong edition mutated download directory")
			}
		}
	}
	selected, err := testGitHub(t, releases, assets).CheckSystem(context.Background(), d, trust, "stable", 0)
	if err != nil || selected == nil {
		t.Fatalf("no correct candidate: %v", err)
	}
	m, err := selected.Verify(d, trust, "stable", 0)
	if err != nil || m.Version != "1.1.0" || m.ProductEdition != "public" {
		t.Fatalf("selected wrong edition: %+v %v", m, err)
	}
	d.ProductEdition = "private"
	selected, err = testGitHub(t, releases, assets).CheckSystem(context.Background(), d, trust, "stable", 0)
	if err != nil || selected == nil {
		t.Fatalf("no private candidate: %v", err)
	}
	m, err = selected.Verify(d, trust, "stable", 0)
	if err != nil || m.ProductEdition != "private" || m.Version != "1.2.0" {
		t.Fatalf("private candidate mismatch: %+v %v", m, err)
	}
}

func TestSystemProductMarkerAndJournalFence(t *testing.T) {
	for _, edition := range []string{"public", "private"} {
		for _, raw := range []string{edition, edition + "\n"} {
			if err := validateSystemEditionMarker([]byte(raw), edition); err != nil {
				t.Fatal(err)
			}
		}
		for _, raw := range []string{"", "unknown\n", "public\nprivate\n", " " + edition, edition + "\n\n"} {
			if err := validateSystemEditionMarker([]byte(raw), edition); err == nil {
				t.Fatal("unsafe marker accepted")
			}
		}
	}
	if err := validateSystemEditionMarker([]byte("public\n"), "private"); err == nil {
		t.Fatal("cross edition marker accepted")
	}
	if err := validateSystemEditionMarker([]byte("public\n"), ""); err == nil {
		t.Fatal("new marker accepted legacy descriptor")
	}
	missing := filepath.Join(t.TempDir(), "absent")
	if err := systemEditionMarker(missing, ""); err != nil {
		t.Fatal("legacy missing marker rejected")
	}
	if err := systemEditionMarker(missing, "private"); err == nil {
		t.Fatal("new edition accepted missing marker")
	}
	for _, phase := range []string{"applying", "pending-reboot"} {
		if err := checkSystemProductCommit(Config{}, SystemJournal{Phase: phase, ProductEdition: "private"}); err == nil {
			t.Fatal("pending product commit accepted without controlled baseline")
		}
	}
	for _, phase := range []string{"idle", "committed"} {
		if err := checkSystemProductCommit(Config{}, SystemJournal{Phase: phase}); err != nil {
			t.Fatal(err)
		}
	}
}
