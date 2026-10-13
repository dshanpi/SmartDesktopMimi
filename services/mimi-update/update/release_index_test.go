package update

import (
	"context"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func signedIndex(t *testing.T, x ReleaseIndex) ([]byte, []byte) {
	t.Helper()
	raw, err := json.Marshal(x)
	if err != nil {
		t.Fatal(err)
	}
	h := sha256.Sum256(raw)
	sig, err := rsa.SignPKCS1v15(rand.Reader, signingKey(t), crypto.SHA256, h[:])
	if err != nil {
		t.Fatal(err)
	}
	return raw, sig
}

func indexFixture(t *testing.T) (ReleaseIndex, Device, Trust, map[string][]byte) {
	m, d, trust := fixture(t)
	x := ReleaseIndex{Schema: 1, Policy: Policy, Type: "application-index", Repository: "test/host-only", Version: m.Version, Commit: m.Commit, Channel: m.Channel, Sequence: m.Sequence, KeyID: m.KeyID}
	files := map[string][]byte{}
	for _, id := range []string{"a133", "t527", "rk3576"} {
		pm := clone(m)
		pm.Targets[0].Identity["platform_id"] = id
		if id != "a133" {
			pm.Targets[0].Identity["soc_id"] = id
		}
		pm, raw, sig, payload := makePackage(t, pm)
		p := IndexedPackage{ID: id, Manifest: id + "--manifest.json", Signature: id + "--manifest.sig", Archive: id + "--application.tar.gz", ManifestSHA256: digestBytes(raw), SignatureSHA256: digestBytes(sig), PackageSHA256: pm.PackageSHA256, PackageBytes: pm.PackageBytes}
		x.Packages = append(x.Packages, p)
		files[p.Manifest] = raw
		files[p.Signature] = sig
		files[p.Archive] = payload
	}
	return x, d, trust, files
}

func indexedGitHub(t *testing.T, x ReleaseIndex, files map[string][]byte) *GitHub {
	raw, sig := signedIndex(t, x)
	files["release-index.json"] = raw
	files["release-index.sig"] = sig
	assets := map[string][]byte{}
	var entries []Asset
	for name, b := range files {
		url := "https://github.com/test/host-only/releases/download/v" + x.Version + "/" + name
		entries = append(entries, Asset{name, url, int64(len(b))})
		assets[url] = b
	}
	return testGitHub(t, []Release{{"v" + x.Version, false, x.Channel == "testing", entries}}, assets)
}

func TestIndexedSelectionAllPlatforms(t *testing.T) {
	x, d, trust, files := indexFixture(t)
	for _, platform := range []string{"a133", "t527", "rk3576"} {
		t.Run(platform, func(t *testing.T) {
			device := clone(d)
			device.Identity["platform_id"] = platform
			if platform != "a133" {
				device.Identity["soc_id"] = platform
			}
			g := indexedGitHub(t, x, clone(files))
			c, err := g.Check(context.Background(), device, trust, "stable", "1.0.0", 0)
			if err != nil || c == nil || c.Package.Name != platform+"--application.tar.gz" {
				t.Fatalf("selection: %v %v", c, err)
			}
			if _, err := g.Download(context.Background(), c, t.TempDir()); err != nil {
				t.Fatal(err)
			}
		})
	}
	wrong := clone(d)
	wrong.Identity["hardware_revision"] = "unknown-r9"
	if c, err := indexedGitHub(t, x, files).Check(context.Background(), wrong, trust, "stable", "1.0.0", 0); err != nil || c != nil {
		t.Fatalf("wrong board selected: %v %v", c, err)
	}
}

func TestIndexedReleaseRejectsTamperingAndAmbiguity(t *testing.T) {
	for _, mode := range []string{"manifest-hash", "source", "missing", "duplicate-id", "ambiguous", "repository", "revoked", "path", "package-size", "old-sequence"} {
		t.Run(mode, func(t *testing.T) {
			x, d, trust, files := indexFixture(t)
			switch mode {
			case "manifest-hash":
				files[x.Packages[0].Manifest] = append(files[x.Packages[0].Manifest], ' ')
			case "source":
				x.Commit = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
			case "missing":
				delete(files, x.Packages[0].Archive)
			case "duplicate-id":
				x.Packages = append(x.Packages, x.Packages[0])
			case "ambiguous":
				p := &x.Packages[1]
				a := x.Packages[0]
				files[p.Manifest] = files[a.Manifest]
				files[p.Signature] = files[a.Signature]
				p.ManifestSHA256 = a.ManifestSHA256
				p.SignatureSHA256 = a.SignatureSHA256
			case "repository":
				x.Repository = "test/other"
			case "revoked":
				trust.Revoked = []string{"test"}
			case "path":
				x.Packages[0].Archive = "../application.tar.gz"
			case "package-size":
				x.Packages[0].PackageBytes++
			case "old-sequence":
				trust.MinSequence = x.Sequence + 1
			}
			// A valid legacy package must never rescue a broken indexed release.
			m, _, _ := fixture(t)
			_, raw, sig, payload := makePackage(t, m)
			files["manifest.json"] = raw
			files["manifest.sig"] = sig
			files["application.tar.gz"] = payload
			if c, err := indexedGitHub(t, x, files).Check(context.Background(), d, trust, "stable", "1.0.0", 0); err == nil || c != nil {
				t.Fatalf("invalid index accepted: %v %v", c, err)
			}
		})
	}
}

func TestIndexOfflineDirectoryAndSignature(t *testing.T) {
	x, _, trust, files := indexFixture(t)
	raw, sig := signedIndex(t, x)
	dir := t.TempDir()
	for name, b := range files {
		if err := os.WriteFile(filepath.Join(dir, name), b, 0600); err != nil {
			t.Fatal(err)
		}
	}
	if err := VerifyIndexDirectory(raw, sig, trust, dir); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "release-index.json"), raw, 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "release-index.sig"), sig, 0600); err != nil {
		t.Fatal(err)
	}
	_, device, _ := fixture(t)
	candidate, err := SelectIndexDirectory(dir, x.Repository, x.Channel, device, trust)
	if err != nil || candidate == nil || candidate.Package.Name != "a133--application.tar.gz" {
		t.Fatalf("offline selection: %v %v", candidate, err)
	}
	if _, err := SelectIndexDirectory(dir, "test/wrong", x.Channel, device, trust); err == nil {
		t.Fatal("offline repository mismatch accepted")
	}
	bad := append([]byte{}, sig...)
	bad[0] ^= 1
	if _, err := VerifyIndex(raw, bad, trust); err == nil {
		t.Fatal("invalid index signature accepted")
	}
	if err := os.WriteFile(filepath.Join(dir, x.Packages[0].Archive), []byte("damaged"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := VerifyIndexDirectory(raw, sig, trust, dir); err == nil {
		t.Fatal("damaged archive accepted offline")
	}
}
