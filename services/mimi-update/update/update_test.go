package update

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"debug/elf"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"
)

var testKey *rsa.PrivateKey
var keyOnce sync.Once

func signingKey(t *testing.T) *rsa.PrivateKey {
	t.Helper()
	keyOnce.Do(func() {
		var e error
		testKey, e = rsa.GenerateKey(rand.Reader, 3072)
		if e != nil {
			panic(e)
		}
	})
	return testKey
}
func fixture(t *testing.T) (Manifest, Device, Trust) {
	t.Helper()
	id := map[string]string{"product_id": "smartdesktopmimi", "platform_id": "a133", "soc_id": "allwinner-a133-b6", "board_id": "B6", "hardware_revision": "test-only-r1", "variant": "uart0", "architecture": "aarch64", "isa": "armv8-a", "bits": "64", "endian": "little", "abi": "aapcs64", "libc": "glibc", "loader": "/lib/ld-linux-aarch64.so.1", "ports_abi": "1", "ipc_contract": "v1-test-contract"}
	ranges := map[string]Range{}
	runtime := map[string]string{}
	for _, k := range runtimeKeys {
		ranges[k] = Range{Min: "1.0.0", MaxExclusive: "2.0.0", Exclude: []string{}}
		runtime[k] = "1.0.0"
	}
	m := Manifest{Schema: 1, Policy: Policy, Type: "application", Version: "1.1.0", Commit: strings.Repeat("a", 40), Channel: "stable", Sequence: 2, KeyID: "test", Targets: []Target{{Identity: id, Runtime: ranges, Capabilities: []string{"desktop"}}}, Data: DataRange{Min: 1, Max: 1, Write: 1}}
	d := Device{Schema: 1, Identity: map[string]string{}, Runtime: runtime, Capabilities: []string{"desktop"}, DataSchema: 1, SystemBaseline: "host-fixture-only", Enrollment: "host-test-not-hardware"}
	for k, v := range id {
		d.Identity[k] = v
	}
	der, e := x509.MarshalPKIXPublicKey(&signingKey(t).PublicKey)
	if e != nil {
		t.Fatal(e)
	}
	trust := Trust{Schema: 1, Keys: map[string]string{"test": string(pem.EncodeToMemory(&pem.Block{Type: "PUBLIC KEY", Bytes: der}))}}
	return m, d, trust
}
func makePackage(t *testing.T, m Manifest) (Manifest, []byte, []byte, []byte) {
	t.Helper()
	payload := []byte("test fixture desktop " + m.Version)
	var buf bytes.Buffer
	gz := gzip.NewWriter(&buf)
	tw := tar.NewWriter(gz)
	tw.WriteHeader(&tar.Header{Name: "bin/lvglsim", Mode: 0755, Size: int64(len(payload)), Typeflag: tar.TypeReg})
	tw.Write(payload)
	tw.Close()
	gz.Close()
	b := buf.Bytes()
	h := sha256.Sum256(b)
	ph := sha256.Sum256(payload)
	m.PackageSHA256 = hex.EncodeToString(h[:])
	m.PackageBytes = int64(len(b))
	m.ExpandedBytes = int64(len(payload))
	m.MaxFiles = 1
	m.Files = map[string]File{"bin/lvglsim": {SHA256: hex.EncodeToString(ph[:]), Size: int64(len(payload)), Mode: 0755}}
	raw, sig := sign(t, m)
	return m, raw, sig, b
}
func sign(t *testing.T, m Manifest) ([]byte, []byte) {
	t.Helper()
	raw, e := json.Marshal(m)
	if e != nil {
		t.Fatal(e)
	}
	h := sha256.Sum256(raw)
	sig, e := rsa.SignPKCS1v15(rand.Reader, signingKey(t), crypto.SHA256, h[:])
	if e != nil {
		t.Fatal(e)
	}
	return raw, sig
}
func clone[T any](v T) T { b, _ := json.Marshal(v); var c T; json.Unmarshal(b, &c); return c }
func TestSemver(t *testing.T) {
	for _, c := range []struct {
		a, b string
		want int
	}{{"1.9.0", "1.10.0", -1}, {"1.0.0-rc.9", "1.0.0-rc.10", -1}, {"1.0.0-rc.1", "1.0.0", -1}, {"1.0.0+build", "1.0.0", 0}, {"1.0.0-alpha", "1.0.0-1", 1}, {"1.0.0-a", "1.0.0-a.1", -1}} {
		got, e := Compare(c.a, c.b)
		if e != nil || got != c.want {
			t.Fatalf("%+v got %d %v", c, got, e)
		}
	}
	for _, v := range []string{"1.2", "01.2.3", "1.2.3-01", "1.2.3-", "*", "unknown", "1.2.3\n"} {
		if _, e := parseVersion(v); e == nil {
			t.Errorf("accepted %q", v)
		}
	}
}
func TestAdmissionMatrix(t *testing.T) {
	m, d, _ := fixture(t)
	m, _, _, _ = makePackage(t, m)
	if e := Match(m, d); e != nil {
		t.Fatal(e)
	}
	for _, k := range identityKeys {
		t.Run(k, func(t *testing.T) {
			bad := clone(d)
			bad.Identity[k] = "mismatch"
			if Match(m, bad) == nil {
				t.Fatal("accepted wrong identity")
			}
			delete(bad.Identity, k)
			if Match(m, bad) == nil {
				t.Fatal("accepted missing identity")
			}
		})
	}
	for _, k := range runtimeKeys {
		t.Run("runtime-"+k, func(t *testing.T) {
			for _, v := range []string{"0.9.0", "2.0.0", "unknown", ""} {
				bad := clone(d)
				bad.Runtime[k] = v
				if Match(m, bad) == nil {
					t.Fatalf("accepted %s", v)
				}
			}
		})
	}
	for name, mutate := range map[string]func(*Device){"missing-capability": func(d *Device) { d.Capabilities = nil }, "missing-enrollment": func(d *Device) { d.Enrollment = "" }, "wrong-data": func(d *Device) { d.DataSchema = 2 }, "unknown-schema": func(d *Device) { d.Schema = 2 }} {
		t.Run(name, func(t *testing.T) {
			bad := clone(d)
			mutate(&bad)
			if Match(m, bad) == nil {
				t.Fatal("accepted")
			}
		})
	}
	a := clone(m.Targets[0])
	a.Identity["board_id"] = "other-board"
	b := clone(m.Targets[0])
	b.Identity["platform_id"] = "rk3576"
	m.Targets = []Target{a, b}
	if Match(m, d) == nil {
		t.Fatal("cross-product incorrectly matched")
	}
}
func TestStrictJSON(t *testing.T) {
	for _, raw := range []string{`{"schema_version":1,"schema_version":1}`, `{"schema_version":1,"unknown":2}`, `{} {}`, `{"identity":{"board_id":"a","board_id":"b"}}`} {
		var d Device
		if StrictJSON([]byte(raw), &d) == nil {
			t.Fatal("accepted ambiguous JSON", raw)
		}
	}
}
func TestTrustAndTampering(t *testing.T) {
	m, _, trust := fixture(t)
	m, raw, sig, _ := makePackage(t, m)
	if _, e := Verify(raw, sig, trust); e != nil {
		t.Fatal(e)
	}
	bad := append([]byte{}, raw...)
	bad = bytes.Replace(bad, []byte("1.1.0"), []byte("1.9.0"), 1)
	if _, e := Verify(bad, sig, trust); e == nil {
		t.Fatal("tampered manifest accepted")
	}
	sig[0] ^= 1
	if _, e := Verify(raw, sig, trust); e == nil {
		t.Fatal("bad signature accepted")
	}
	raw, sig = sign(t, m)
	trust.Revoked = []string{"test"}
	if _, e := Verify(raw, sig, trust); e == nil {
		t.Fatal("revoked key")
	}
	trust.Revoked = nil
	trust.MinSequence = 3
	if _, e := Verify(raw, sig, trust); e == nil {
		t.Fatal("replayed manifest")
	}
}
func TestArchiveAttacks(t *testing.T) {
	m, _, _ := fixture(t)
	m, _, _, good := makePackage(t, m)
	dir := t.TempDir()
	p := filepath.Join(dir, "p.tar.gz")
	os.WriteFile(p, good, 0600)
	if e := CheckArchive(p, m, ""); e != nil {
		t.Fatal(e)
	}
	for _, name := range []string{"../evil", "/etc/passwd", "fonts/../../evil", "etc/device.json", "bin/lv_backend"} {
		if allowedPath(name) {
			t.Fatal("allowed dangerous path", name)
		}
	}
	for _, kind := range []byte{tar.TypeSymlink, tar.TypeLink, tar.TypeChar, tar.TypeFifo} {
		var buf bytes.Buffer
		gz := gzip.NewWriter(&buf)
		tw := tar.NewWriter(gz)
		tw.WriteHeader(&tar.Header{Name: "bin/lvglsim", Mode: 0755, Typeflag: kind, Linkname: "/etc/passwd"})
		tw.Close()
		gz.Close()
		b := buf.Bytes()
		h := sha256.Sum256(b)
		bad := m
		bad.PackageSHA256 = hex.EncodeToString(h[:])
		bad.PackageBytes = int64(len(b))
		os.WriteFile(p, b, 0600)
		if CheckArchive(p, bad, "") == nil {
			t.Fatalf("accepted tar type %d", kind)
		}
	}
	os.WriteFile(p, append(good, 0), 0600)
	if CheckArchive(p, m, "") == nil {
		t.Fatal("corruption accepted")
	}
}

type fakeHooks struct {
	d           Device
	busy        bool
	healthFail  bool
	storageFail bool
	starts      []string
	stops       int
}

func (h *fakeHooks) Preflight() error {
	if h.storageFail {
		return fmt.Errorf("storage preflight: EXT4 errors")
	}
	return nil
}
func (h *fakeHooks) Device() (Device, error) { return h.d, nil }
func (h *fakeHooks) Idle() error {
	if h.busy {
		return fmt.Errorf("voice active")
	}
	return nil
}
func (h *fakeHooks) Stop() error            { h.stops++; return nil }
func (h *fakeHooks) Start(dir string) error { h.starts = append(h.starts, dir); return nil }
func (h *fakeHooks) Healthy(_ string, _ time.Duration) error {
	if h.healthFail {
		return fmt.Errorf("UI loop stalled")
	}
	return nil
}
func engineFixture(t *testing.T) (*Engine, *fakeHooks, Manifest, []byte, []byte, string) {
	t.Helper()
	m, d, trust := fixture(t)
	root := t.TempDir()
	factory := filepath.Join(root, "factory")
	os.Mkdir(factory, 0755)
	old := clone(m)
	old.Version = "1.0.0"
	old.Sequence = 1
	old, raw, sig, b := makePackage(t, old)
	p := filepath.Join(root, "factory.tar.gz")
	os.WriteFile(p, b, 0600)
	if e := CheckArchive(p, old, factory); e != nil {
		t.Fatal(e)
	}
	os.WriteFile(filepath.Join(factory, "manifest.json"), raw, 0644)
	os.WriteFile(filepath.Join(factory, "manifest.sig"), sig, 0644)
	m, raw, sig, b = makePackage(t, m)
	p = filepath.Join(root, "candidate.tar.gz")
	os.WriteFile(p, b, 0600)
	hooks := &fakeHooks{d: d}
	e := &Engine{Root: filepath.Join(root, "data"), Factory: factory, Lock: filepath.Join(root, "update.lock"), Hooks: hooks, Trust: func() (Trust, error) { return trust, nil }}
	return e, hooks, m, raw, sig, p
}
func TestInstallCommitAndReplay(t *testing.T) {
	e, h, m, raw, sig, p := engineFixture(t)
	if err := e.Install(p, raw, sig, "stable"); err != nil {
		t.Fatal(err)
	}
	s, _ := e.Status()
	if s.Trial || s.Current != m.PackageSHA256 || s.Previous != "factory" || len(h.starts) != 1 {
		t.Fatalf("bad commit %+v", s)
	}
	if err := e.Install(p, raw, sig, "stable"); err == nil {
		t.Fatal("replay accepted")
	}
	dir, err := e.Select()
	if err != nil || dir != e.dir(m.PackageSHA256) {
		t.Fatal(dir, err)
	}
}
func TestBadHealthRollsBack(t *testing.T) {
	e, h, m, raw, sig, p := engineFixture(t)
	h.healthFail = true
	if err := e.Install(p, raw, sig, "stable"); err == nil {
		t.Fatal("stalled UI committed")
	}
	s, _ := e.Status()
	if s.Trial || s.Current != "factory" || s.HighWater != m.Sequence {
		t.Fatalf("bad rollback %+v", s)
	}
	h.healthFail = false
	if err := e.Install(p, raw, sig, "stable"); err == nil {
		t.Fatal("failed candidate retried")
	}
}
func TestCrashRecovery(t *testing.T) {
	for _, point := range []string{"extracted", "staged", "journaled", "stopped", "started", "healthy"} {
		t.Run(point, func(t *testing.T) {
			e, _, _, raw, sig, p := engineFixture(t)
			e.Fault = func(got string) error {
				if got == point {
					return fmt.Errorf("simulated crash at %s", got)
				}
				return nil
			}
			if err := e.Install(p, raw, sig, "stable"); err == nil {
				t.Fatal("fault missing")
			}
			e.Fault = nil
			dir, err := e.Select()
			if err != nil || dir != e.Factory {
				t.Fatalf("failed recovery %s %v", dir, err)
			}
			s, _ := e.Status()
			if s.Trial {
				t.Fatal("trial not cleared")
			}
		})
	}
}
func TestBusyAndConcurrentUpdates(t *testing.T) {
	e, h, _, raw, sig, p := engineFixture(t)
	h.busy = true
	if e.Install(p, raw, sig, "stable") == nil || h.stops != 0 {
		t.Fatal("interrupted active voice")
	}
	h.busy = false
	unlock, err := e.lock()
	if err != nil {
		t.Fatal(err)
	}
	defer unlock()
	if e.Install(p, raw, sig, "stable") == nil || h.stops != 0 {
		t.Fatal("parallel update accepted")
	}
}
func TestDataAndInstalledCorruption(t *testing.T) {
	e, _, m, raw, sig, p := engineFixture(t)
	os.MkdirAll(e.Root, 0700)
	sentinel := filepath.Join(e.Root, "user-data")
	os.WriteFile(sentinel, []byte("keep me"), 0600)
	if err := e.Install(p, raw, sig, "stable"); err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(e.dir(m.PackageSHA256), "bin/lvglsim"), []byte("corrupt"), 0755)
	dir, err := e.Select()
	if err != nil || dir != e.Factory {
		t.Fatal("no corruption rollback", err)
	}
	b, _ := os.ReadFile(sentinel)
	if string(b) != "keep me" {
		t.Fatal("user data changed")
	}
}
func TestSystemRollbackRejectsIncompatibleApp(t *testing.T) {
	e, h, m, _, _, _ := engineFixture(t)
	m.Targets[0].Runtime["system"] = Range{Min: "1.1.0", MaxExclusive: "2.0.0"}
	m, raw, sig, b := makePackage(t, m)
	p := filepath.Join(t.TempDir(), "update.gz")
	os.WriteFile(p, b, 0600)
	h.d.Runtime["system"] = "1.1.0"
	if err := e.Install(p, raw, sig, "stable"); err != nil {
		t.Fatal(err)
	}
	h.d.Runtime["system"] = "1.0.0"
	dir, err := e.Select()
	if err != nil || dir != e.Factory {
		t.Fatal("loaded app on incompatible old system", err)
	}
}
func TestCorruptJournalFailsClosed(t *testing.T) {
	e, _, _, _, _, _ := engineFixture(t)
	os.MkdirAll(e.Root, 0700)
	os.WriteFile(filepath.Join(e.Root, "state.json"), []byte(`{"current":"../../etc"}`), 0600)
	if _, err := e.Select(); err == nil {
		t.Fatal("corrupt journal reset silently")
	}
}
func TestHTTPRejectsMissingCandidate(t *testing.T) {
	e, _, _, _, _, _ := engineFixture(t)
	s := &Service{Engine: e, phase: "idle"}
	for _, path := range []string{"/install", "/download", "/cancel"} {
		w := httptest.NewRecorder()
		s.Handler().ServeHTTP(w, httptest.NewRequest("POST", path, nil))
		if w.Code != http.StatusConflict {
			t.Fatal(path, w.Code)
		}
	}
}
func TestURLPolicy(t *testing.T) {
	for _, url := range []string{"http://github.com/x", "https://evil.test/x", "https://github.com.evil.test/x", "https://user:pass@github.com/x", "https://github.com:123/x"} {
		if safeGitHubURL(url) {
			t.Fatal(url)
		}
	}
}

func TestPackRealELFAndDeterministicSignature(t *testing.T) {
	m, d, trust := fixture(t)
	// /bin/true is GNU coreutils on the host and BusyBox on the A133.
	// Keep the positive fixture native without weakening ELF admission checks.
	switch runtime.GOARCH {
	case "amd64":
		m.Targets[0].Identity["architecture"] = "x86_64"
		m.Targets[0].Identity["isa"] = "x86-64"
		m.Targets[0].Identity["loader"] = "/lib64/ld-linux-x86-64.so.2"
	case "arm64":
		m.Targets[0].Identity["architecture"] = "aarch64"
		m.Targets[0].Identity["isa"] = "armv8-a"
		m.Targets[0].Identity["loader"] = "/lib/ld-linux-aarch64.so.1"
	default:
		t.Fatalf("unsupported native packaging test architecture: %s", runtime.GOARCH)
	}
	native, err := elf.Open("/bin/true")
	if err != nil {
		t.Fatal(err)
	}
	libraries, err := native.ImportedLibraries()
	native.Close()
	if err != nil {
		t.Fatal(err)
	}
	for _, library := range libraries {
		// These are fixture versions, not claims about installed library versions.
		m.Targets[0].Runtime["soname:"+library] = Range{Min: "1.0.0", MaxExclusive: "2.0.0"}
		d.Runtime["soname:"+library] = "1.0.0"
	}
	d.Identity = clone(m.Targets[0].Identity)
	root := t.TempDir()
	source := filepath.Join(root, "source")
	os.MkdirAll(filepath.Join(source, "bin"), 0755)
	payload, e := os.ReadFile("/bin/true")
	if e != nil {
		t.Fatal(e)
	}
	os.WriteFile(filepath.Join(source, "bin/lvglsim"), payload, 0755)
	key := filepath.Join(root, "key.pem")
	os.WriteFile(key, pem.EncodeToMemory(&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(signingKey(t))}), 0600)
	for _, name := range []string{"one", "two"} {
		if e = Pack(source, filepath.Join(root, name), key, m, 1700000000); e != nil {
			t.Fatal(e)
		}
	}
	for _, name := range []string{"application.tar.gz", "manifest.json", "manifest.sig"} {
		a, _ := os.ReadFile(filepath.Join(root, "one", name))
		b, _ := os.ReadFile(filepath.Join(root, "two", name))
		if !bytes.Equal(a, b) {
			t.Fatal("nondeterministic", name)
		}
	}
	raw, _ := os.ReadFile(filepath.Join(root, "one/manifest.json"))
	sig, _ := os.ReadFile(filepath.Join(root, "one/manifest.sig"))
	signed, e := Verify(raw, sig, trust)
	if e != nil {
		t.Fatal(e)
	}
	if e = Match(signed, d); e != nil {
		t.Fatal(e)
	}
	if e = CheckArchive(filepath.Join(root, "one/application.tar.gz"), signed, ""); e != nil {
		t.Fatal(e)
	}
	os.WriteFile(filepath.Join(source, "bin/lv_backend"), payload, 0755)
	if Pack(source, filepath.Join(root, "forbidden"), key, m, 1700000000) == nil {
		t.Fatal("backend bundled as application")
	}
}

func TestRangeExclusionsAndMigrationGate(t *testing.T) {
	m, d, _ := fixture(t)
	m, _, _, _ = makePackage(t, m)
	r := m.Targets[0].Runtime["system"]
	r.Exclude = []string{"1.0.0"}
	m.Targets[0].Runtime["system"] = r
	if Match(m, d) == nil {
		t.Fatal("excluded system accepted")
	}
	r.Exclude = nil
	m.Targets[0].Runtime["system"] = r
	m.Data.Max = 2
	m.Data.Write = 2
	if Match(m, d) == nil {
		t.Fatal("irreversible data migration accepted")
	}
}

// Run the real transaction in a process that exits without defers. This is not
// physical power-loss evidence, but catches dependence on graceful cleanup.
func TestCrashChild(t *testing.T) {
	root := os.Getenv("MIMI_TEST_CRASH_ROOT")
	if root == "" {
		return
	}
	var d Device
	var trust Trust
	if e := ReadJSON(filepath.Join(root, "device.json"), &d); e != nil {
		t.Fatal(e)
	}
	if e := ReadJSON(filepath.Join(root, "trust.json"), &trust); e != nil {
		t.Fatal(e)
	}
	e := &Engine{Root: filepath.Join(root, "data"), Factory: filepath.Join(root, "factory"), Lock: filepath.Join(root, "update.lock"), Hooks: &fakeHooks{d: d}, Trust: func() (Trust, error) { return trust, nil }, Fault: func(point string) error {
		if point == os.Getenv("MIMI_TEST_CRASH_POINT") {
			os.Exit(77)
		}
		return nil
	}}
	raw, _ := os.ReadFile(filepath.Join(root, "candidate.json"))
	sig, _ := os.ReadFile(filepath.Join(root, "candidate.sig"))
	if err := e.Install(filepath.Join(root, "candidate.tar.gz"), raw, sig, "stable"); err != nil {
		t.Fatal(err)
	}
	t.Fatal("crash point never reached")
}
func TestAbruptProcessTerminationRecovery(t *testing.T) {
	for _, point := range []string{"extracted", "staged", "journaled", "stopped", "started", "healthy"} {
		t.Run(point, func(t *testing.T) {
			engine, h, _, raw, sig, _ := engineFixture(t)
			root := filepath.Dir(engine.Root)
			trust, _ := engine.Trust()
			for name, value := range map[string]any{"device.json": h.d, "trust.json": trust} {
				b, _ := json.Marshal(value)
				os.WriteFile(filepath.Join(root, name), b, 0600)
			}
			os.WriteFile(filepath.Join(root, "candidate.json"), raw, 0600)
			os.WriteFile(filepath.Join(root, "candidate.sig"), sig, 0600)
			child := exec.Command(os.Args[0], "-test.run=^TestCrashChild$")
			child.Env = append(os.Environ(), "MIMI_TEST_CRASH_ROOT="+root, "MIMI_TEST_CRASH_POINT="+point)
			output, err := child.CombinedOutput()
			exit, ok := err.(*exec.ExitError)
			if !ok || exit.ExitCode() != 77 {
				t.Fatalf("unexpected subprocess exit: %v %s", err, output)
			}
			dir, err := engine.Select()
			if err != nil || dir != engine.Factory {
				t.Fatalf("failed recovery after process death: %s %v", dir, err)
			}
		})
	}
}

func TestSystemCommitKeepsSharedUpdateLock(t *testing.T) {
	e, _, _, _, _, _ := engineFixture(t)
	called := false
	err := e.SystemHealth(func() error {
		called = true
		unlock, err := e.lock()
		if err == nil {
			unlock()
			t.Fatal("system commit lost mutual exclusion")
		}
		return nil
	})
	if err != nil || !called {
		t.Fatal("health/commit failed", err)
	}
}

type launchProbe struct {
	Hooks
	start func(string) error
}

func (h launchProbe) Start(dir string) error { return h.start(dir) }

func TestStartSelectedKeepsSharedUpdateLock(t *testing.T) {
	e, hooks, _, _, _, _ := engineFixture(t)
	called := false
	e.Hooks = launchProbe{Hooks: hooks, start: func(dir string) error {
		called = true
		if dir != e.Factory {
			t.Fatalf("started unselected directory %q", dir)
		}
		if err := e.Install("", nil, nil, "stable"); err == nil || !strings.Contains(err.Error(), "another application/system update") {
			t.Fatalf("installation entered during platform launch: %v", err)
		}
		return fmt.Errorf("platform launch failed")
	}}
	if err := e.StartSelected(); err == nil || err.Error() != "platform launch failed" || !called {
		t.Fatalf("launch failure was lost: %v, called=%v", err, called)
	}
	unlock, err := e.lock()
	if err != nil {
		t.Fatal("failed launch leaked shared lock", err)
	}
	unlock()
}

func TestStartSelectedRejectsUntrustedTreeBeforeLaunch(t *testing.T) {
	e, hooks, _, _, _, _ := engineFixture(t)
	if err := os.WriteFile(filepath.Join(e.Factory, "manifest.sig"), []byte("tampered"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := e.StartSelected(); err == nil {
		t.Fatal("started an untrusted factory tree")
	}
	if len(hooks.starts) != 0 {
		t.Fatal("platform launched before admission")
	}
}
