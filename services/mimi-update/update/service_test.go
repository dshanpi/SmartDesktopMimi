package update

import (
	"bytes"
	"context"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }
func testGitHub(t *testing.T, releases []Release, assets map[string][]byte) *GitHub {
	t.Helper()
	g, err := NewGitHub("test/host-only")
	if err != nil {
		t.Fatal(err)
	}
	g.Client.Transport = roundTripFunc(func(r *http.Request) (*http.Response, error) {
		var body []byte
		if r.URL.Host == "api.github.com" {
			body, _ = json.Marshal(releases)
		} else {
			body = assets[r.URL.String()]
		}
		if body == nil {
			return nil, fmt.Errorf("unexpected request %s", r.URL)
		}
		return &http.Response{StatusCode: 200, Header: make(http.Header), Body: io.NopCloser(bytes.NewReader(body)), ContentLength: int64(len(body)), Request: r}, nil
	})
	return g
}
func TestGitHubSignedSelectionAndDownload(t *testing.T) {
	base, d, trust := fixture(t)
	assets := map[string][]byte{}
	var releases []Release
	add := func(version, board string, draft, pre, tamper bool) {
		m := clone(base)
		m.Version = version
		m.Targets[0].Identity["board_id"] = board
		if pre {
			m.Channel = "testing"
		}
		m, raw, sig, payload := makePackage(t, m)
		prefix := "https://github.com/test/host-only/releases/download/v" + version + "/"
		if tamper {
			raw = bytes.Replace(raw, []byte(version), []byte("9.9.9"), 1)
		}
		entries := []Asset{}
		for name, b := range map[string][]byte{"manifest.json": raw, "manifest.sig": sig, "application.tar.gz": payload} {
			assets[prefix+name] = b
			entries = append(entries, Asset{name, prefix + name, int64(len(b))})
		}
		releases = append(releases, Release{"v" + version, draft, pre, entries})
	}
	add("1.9.0", "B6", false, false, false)
	add("1.10.0", "B6", false, false, false)
	add("2.0.0", "OtherBoard", false, false, false)
	add("3.0.0", "B6", true, false, false)
	add("4.0.0-rc.1", "B6", false, true, false)
	add("5.0.0", "B6", false, false, true)
	g := testGitHub(t, releases, assets)
	c, err := g.Check(context.Background(), d, trust, "stable", "1.0.0", 1)
	if err != nil || c == nil || c.Manifest.Version != "1.10.0" {
		t.Fatalf("wrong compatible numeric candidate: %+v %v", c, err)
	}
	var progress int64
	g.Progress = func(n, total int64) {
		progress = n
		if n > total {
			t.Fatal("unbounded progress")
		}
	}
	archive, err := g.Download(context.Background(), c, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	if progress != c.Manifest.PackageBytes {
		t.Fatal("missing complete progress")
	}
	if err = CheckArchive(archive, c.Manifest, ""); err != nil {
		t.Fatal(err)
	}
	assets[c.Package.URL] = []byte("corrupted payload")
	dir := t.TempDir()
	if _, err = g.Download(context.Background(), c, dir); err == nil {
		t.Fatal("corrupt download accepted")
	}
	entries, _ := os.ReadDir(dir)
	if len(entries) != 0 {
		t.Fatal("failed download was retained")
	}
	if c, err = g.Check(context.Background(), d, trust, "stable", "1.0.0", 2); err != nil || c != nil {
		t.Fatalf("sequence replay selected: %v", err)
	}
}
func TestUserPolicyIsAtomicAndCannotAlterTrust(t *testing.T) {
	e, _, m, _, _, _ := engineFixture(t)
	c := Config{Root: e.Root, Repository: "test/host-only", Channel: "stable"}
	s, err := NewService(c)
	if err != nil {
		t.Fatal(err)
	}
	s.Engine = e
	s.candidate = &Candidate{Manifest: m}
	s.archive = filepath.Join(t.TempDir(), "download.tar.gz")
	os.WriteFile(s.archive, []byte("stale"), 0600)
	request := func(body string) int {
		r := httptest.NewRecorder()
		s.Handler().ServeHTTP(r, httptest.NewRequest("PUT", "/policy", strings.NewReader(body)))
		return r.Code
	}
	valid := `{"schema_version":1,"channel":"testing","auto_download":true,"install_when_idle":true}`
	s.busy = true
	if got := request(valid); got != 409 {
		t.Fatalf("policy changed while active: %d", got)
	}
	s.busy = false
	for _, body := range []string{`{}`, `{"schema_version":1,"channel":"other"}`, `{"schema_version":1,"channel":"stable","repository":"attacker/repo"}`, `{"schema_version":1,"channel":"stable","channel":"testing"}`, strings.Repeat("x", 2049)} {
		if got := request(body); got != 400 {
			t.Fatalf("accepted invalid policy: %d", got)
		}
	}
	if got := request(valid); got != 204 {
		t.Fatalf("valid policy rejected: %d", got)
	}
	if s.candidate != nil || s.archive != "" {
		t.Fatal("channel switch retained candidate")
	}
	restored, err := NewService(c)
	if err != nil {
		t.Fatal(err)
	}
	if restored.Config.Channel != "testing" || !restored.Config.AutoDownload || !restored.Config.InstallWhenIdle {
		t.Fatal("policy did not persist")
	}
	if restored.Config.Repository != c.Repository {
		t.Fatal("policy changed platform repository")
	}
}
func signSystem(t *testing.T, m SystemManifest) ([]byte, []byte) {
	t.Helper()
	raw, err := json.Marshal(m)
	if err != nil {
		t.Fatal(err)
	}
	hash := sha256.Sum256(raw)
	sig, err := rsa.SignPKCS1v15(rand.Reader, signingKey(t), crypto.SHA256, hash[:])
	if err != nil {
		t.Fatal(err)
	}
	return raw, sig
}
func TestSystemAdmissionMatrix(t *testing.T) {
	app, d, trust := fixture(t)
	payload := []byte("host-only-system-image-fixture")
	hash := sha256.Sum256(payload)
	file := filepath.Join(t.TempDir(), "fixture.img")
	os.WriteFile(file, payload, 0600)
	m := SystemManifest{Schema: 1, Policy: Policy, Type: "system", Version: "1.1.0", Commit: strings.Repeat("a", 40), KeyID: "test", Sequence: 2,
		ImageSHA256: fmt.Sprintf("%x", hash), ImageBytes: int64(len(payload)), Targets: app.Targets, Bootloader: Range{Min: "1.0.0", MaxExclusive: "2.0.0"},
		PartitionSHA256: strings.Repeat("b", 64), Storage: "emmc", MinStorageBytes: 1024, BootMode: "ab", FactoryManifestSHA256: strings.Repeat("c", 64), FactoryApplicationSHA256: strings.Repeat("d", 64), SDKComponentsSHA256: strings.Repeat("e", 64)}
	device := SystemDevice{Device: d, Bootloader: "1.0.0", PartitionSHA256: m.PartitionSHA256, Storage: "emmc", StorageBytes: 2048, BootMode: "ab"}
	raw, sig := signSystem(t, m)
	if _, err := VerifySystem(raw, sig, trust, device, file); err != nil {
		t.Fatal(err)
	}
	for name, mutate := range map[string]func(*SystemDevice){
		"bootloader": func(d *SystemDevice) { d.Bootloader = "2.0.0" }, "partitions": func(d *SystemDevice) { d.PartitionSHA256 = strings.Repeat("f", 64) },
		"storage": func(d *SystemDevice) { d.Storage = "nand" }, "capacity": func(d *SystemDevice) { d.StorageBytes = 512 }, "boot-mode": func(d *SystemDevice) { d.BootMode = "single" },
		"same-isa-wrong-soc": func(d *SystemDevice) { d.Device.Identity["soc_id"] = "rk3576" }, "missing-enrollment": func(d *SystemDevice) { d.Device.Enrollment = "" },
	} {
		t.Run(name, func(t *testing.T) {
			bad := clone(device)
			mutate(&bad)
			if _, err := VerifySystem(raw, sig, trust, bad, file); err == nil {
				t.Fatal("mismatched system accepted")
			}
		})
	}
	sig[0] ^= 1
	if _, err := VerifySystem(raw, sig, trust, device, file); err == nil {
		t.Fatal("bad signature accepted")
	}
	_, sig = signSystem(t, m)
	os.WriteFile(file, []byte("wrong image"), 0600)
	if _, err := VerifySystem(raw, sig, trust, device, file); err == nil {
		t.Fatal("bad image accepted")
	}
}

func TestFactoryExtractionUsesAdmissionAndLeavesNoPartialTree(t *testing.T) {
	m, d, trust := fixture(t)
	m, raw, sig, payload := makePackage(t, m)
	dir := t.TempDir()
	archive := filepath.Join(dir, "factory.tar.gz")
	os.WriteFile(archive, payload, 0600)
	out := filepath.Join(dir, "factory")
	if err := ExtractFactory(archive, raw, sig, trust, d, out); err != nil {
		t.Fatal(err)
	}
	if err := VerifyTree(out, m); err != nil {
		t.Fatal(err)
	}
	if err := ExtractFactory(archive, raw, sig, trust, d, out); err == nil {
		t.Fatal("overwrote existing factory")
	}
	wrong := clone(d)
	wrong.Identity["board_id"] = "OtherBoard"
	rejected := filepath.Join(dir, "rejected")
	if err := ExtractFactory(archive, raw, sig, trust, wrong, rejected); err == nil {
		t.Fatal("wrong board admitted")
	}
	if _, err := os.Stat(rejected); !os.IsNotExist(err) {
		t.Fatal("wrong-board output exists")
	}
	os.WriteFile(archive, []byte("corrupt"), 0600)
	if err := ExtractFactory(archive, raw, sig, trust, d, rejected); err == nil {
		t.Fatal("corrupt archive extracted")
	}
	if _, err := os.Stat(rejected); !os.IsNotExist(err) {
		t.Fatal("partial factory left behind")
	}
}

func TestStatusDoesNotExposeOrSerializeFullPackageInventory(t *testing.T) {
	e, _, m, _, _, _ := engineFixture(t)
	s := &Service{Engine: e, Config: Config{Root: e.Root, Channel: "stable"}, candidate: &Candidate{Manifest: m}}
	s.candidate.Manifest.Files = map[string]File{}
	for i := 0; i < 20000; i++ {
		s.candidate.Manifest.Files[fmt.Sprintf("resources/%d", i)] = File{SHA256: strings.Repeat("a", 64), Size: 1, Mode: 0644}
	}
	r := httptest.NewRecorder()
	s.Handler().ServeHTTP(r, httptest.NewRequest("GET", "/status", nil))
	if r.Code != 200 || r.Body.Len() > 65536 {
		t.Fatalf("native status exceeded transport budget: %d %d", r.Code, r.Body.Len())
	}
	if strings.Contains(r.Body.String(), "resources/") {
		t.Fatal("full inventory leaked to UI polling")
	}
}
