package update

import (
	"context"
	"crypto/sha256"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func systemFixture(t *testing.T) (SystemCandidate, SystemDevice, Trust, []byte) {
	t.Helper()
	app, d, trust := fixture(t)
	app, raw, sig, _ := makePackage(t, app)
	payload := []byte("signed-host-SWU-fixture-not-flashable")
	hash := sha256.Sum256(payload)
	factoryHash := sha256.Sum256(raw)
	next := clone(d)
	next.Enrollment = ""
	next.Runtime["system"] = "1.1.0"
	next.SystemBaseline = "host-next-baseline"
	m := SystemManifest{Schema: 1, Policy: Policy, Type: "system", Version: "1.1.0", Commit: strings.Repeat("a", 40), KeyID: "test", Sequence: 2,
		ImageSHA256: strings.Repeat("a", 64), ImageBytes: 100, Targets: app.Targets, Bootloader: Range{Min: "1.0.0", MaxExclusive: "2.0.0"}, PartitionSHA256: strings.Repeat("b", 64), Storage: "emmc", MinStorageBytes: 1024, BootMode: "ab", FactoryManifestSHA256: fmt.Sprintf("%x", factoryHash), FactoryApplicationSHA256: app.PackageSHA256, SDKComponentsSHA256: strings.Repeat("c", 64), OTA: &SystemOTA{Channel: "stable", PackageSHA256: fmt.Sprintf("%x", hash), PackageBytes: int64(len(payload)), Device: next}}
	systemRaw, systemSig := signSystem(t, m)
	return SystemCandidate{systemRaw, systemSig, raw, sig, Asset{"system.swu", "https://github.com/test/system/system.swu", int64(len(payload))}}, SystemDevice{Device: d, Bootloader: "1.0.0", PartitionSHA256: m.PartitionSHA256, Storage: "emmc", StorageBytes: 2048, BootMode: "ab"}, trust, payload
}
func TestSystemOTAAdmissionAndDestinationPairing(t *testing.T) {
	c, d, trust, _ := systemFixture(t)
	if _, err := c.Verify(d, trust, "stable", 0); err != nil {
		t.Fatal(err)
	}
	for name, mutate := range map[string]func(*SystemManifest){
		"destination-isa":           func(m *SystemManifest) { m.OTA.Device.Identity["isa"] = "armv9-a" },
		"destination-board":         func(m *SystemManifest) { m.OTA.Device.Identity["board_id"] = "OtherBoard" },
		"destination-version":       func(m *SystemManifest) { m.OTA.Device.Runtime["system"] = "1.2.0" },
		"destination-baseline":      func(m *SystemManifest) { m.OTA.Device.SystemBaseline = d.Device.SystemBaseline },
		"credentials-in-image":      func(m *SystemManifest) { m.OTA.Device.Enrollment = "device-specific" },
		"data-migration":            func(m *SystemManifest) { m.OTA.Device.DataSchema++ },
		"factory-mismatch":          func(m *SystemManifest) { m.FactoryApplicationSHA256 = strings.Repeat("e", 64) },
		"factory-signature-binding": func(m *SystemManifest) { m.FactoryManifestSHA256 = strings.Repeat("e", 64) },
		"incompatible-next-abi":     func(m *SystemManifest) { m.OTA.Device.Identity["ipc_contract"] = "incompatible" },
		"oversized-package":         func(m *SystemManifest) { m.OTA.PackageBytes = 5 << 30 },
	} {
		t.Run(name, func(t *testing.T) {
			bad := clone(c)
			var m SystemManifest
			if err := StrictJSON(bad.Raw, &m); err != nil {
				t.Fatal(err)
			}
			mutate(&m)
			bad.Raw, bad.Signature = signSystem(t, m)
			if _, err := bad.Verify(d, trust, "stable", 0); err == nil {
				t.Fatal("unsafe system accepted")
			}
		})
	}
	if _, err := c.Verify(d, trust, "testing", 0); err == nil {
		t.Fatal("wrong channel accepted")
	}
	if _, err := c.Verify(d, trust, "stable", 2); err == nil {
		t.Fatal("replay accepted")
	}
	bad := clone(c)
	bad.FactorySignature[0] ^= 1
	if _, err := bad.Verify(d, trust, "stable", 0); err == nil {
		t.Fatal("invalid independent app signature accepted")
	}
}
func TestSystemGitHubDownload(t *testing.T) {
	c, d, trust, payload := systemFixture(t)
	assets := map[string][]byte{}
	entries := []Asset{}
	for name, b := range map[string][]byte{"system-manifest.json": c.Raw, "system-manifest.sig": c.Signature, "factory-manifest.json": c.FactoryRaw, "factory-manifest.sig": c.FactorySignature, "system.swu": payload} {
		url := "https://github.com/test/system/" + name
		assets[url] = b
		entries = append(entries, Asset{name, url, int64(len(b))})
	}
	g := testGitHub(t, []Release{{"system-a133-v1.1.0", false, false, entries}, {"system-a133-v99.0.0", true, false, entries}}, assets)
	selected, err := g.CheckSystem(context.Background(), d, trust, "stable", 0)
	if err != nil || selected == nil {
		t.Fatalf("candidate missing: %v", err)
	}
	path, err := g.DownloadSystem(context.Background(), *selected, d, trust, "stable", 0, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(path); string(b) != string(payload) {
		t.Fatal("wrong downloaded data")
	}
	assets[c.Package.URL] = []byte("bad")
	dir := t.TempDir()
	if _, err = g.DownloadSystem(context.Background(), *selected, d, trust, "stable", 0, dir); err == nil {
		t.Fatal("corruption accepted")
	}
	files, _ := os.ReadDir(dir)
	if len(files) != 0 {
		t.Fatal("partial download retained")
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	// The transport fixture does not honor cancellation, so use the real client
	// and an already cancelled context; no network request can be completed.
	real, _ := NewGitHub("test/system")
	if _, err = real.DownloadSystem(ctx, c, d, trust, "stable", 0, t.TempDir()); err == nil {
		t.Fatal("cancelled request succeeded")
	}
}
func TestABDirectionNeverGuesses(t *testing.T) {
	for _, x := range []struct{ line, env, want string }{
		{"root=/dev/mmcblk0p4 rw", "bootA", "now_A_next_B"},
		{"root=/dev/mmcblk0p6 partitions=rootfsA@mmcblk0p4", "bootB", "now_B_next_A"},
		{"root=/dev/mmcblk0p40", "bootA", ""}, {"xroot=/dev/mmcblk0p4", "bootA", ""},
		{"root=/dev/mmcblk0p4 root=/dev/mmcblk0p6", "bootA", ""},
		{"root=/dev/mmcblk0p4", "bootB", ""}, {"root=PARTUUID=unknown", "bootA", ""}, {"", "bootA", ""},
	} {
		got, err := ABDirection(x.line, x.env)
		if got != x.want || (err == nil) != (x.want != "") {
			t.Errorf("%q %q: %q %v", x.line, x.env, got, err)
		}
	}
}
func TestActualA133PartitionGeometry(t *testing.T) {
	root := t.TempDir()
	write := func(name, data string) {
		p := filepath.Join(root, name)
		os.MkdirAll(filepath.Dir(p), 0700)
		if err := os.WriteFile(p, []byte(data), 0600); err != nil {
			t.Fatal(err)
		}
	}
	write("sys/class/block/mmcblk0/size", "10000")
	os.MkdirAll(filepath.Join(root, "dev/by-name"), 0700)
	for i, name := range []string{"bootA", "rootfsA", "bootB", "rootfsB"} {
		dev := fmt.Sprintf("mmcblk0p%d", i+3)
		write("dev/"+dev, "")
		if err := os.Symlink("../"+dev, filepath.Join(root, "dev/by-name", name)); err != nil {
			t.Fatal(err)
		}
		write("sys/class/block/"+dev+"/start", fmt.Sprint(100+i*100))
		write("sys/class/block/"+dev+"/size", "100")
	}
	digest, capacity, err := a133Geometry(root)
	if err != nil || len(digest) != 64 || capacity != 5120000 {
		t.Fatalf("%s %d %v", digest, capacity, err)
	}
	write("sys/class/block/mmcblk0p6/start", "100")
	if _, _, err = a133Geometry(root); err == nil {
		t.Fatal("overlapping geometry accepted")
	}
}
func TestSystemPendingTransactionBlocksApplication(t *testing.T) {
	e, _, m, raw, sig, archive := engineFixture(t)
	boot, err := os.ReadFile("/proc/sys/kernel/random/boot_id")
	if err != nil {
		t.Fatal(err)
	}
	state := SystemJournal{Schema: 1, Phase: "pending-reboot", HighWater: 2, Version: "1.1.0", PackageSHA256: strings.Repeat("a", 64), BootID: strings.TrimSpace(string(boot))}
	if err = atomicJSON(filepath.Join(e.Root, "system"), "state.json", state); err != nil {
		t.Fatal(err)
	}
	if err = e.Install(archive, raw, sig, m.Channel); err == nil || !strings.Contains(err.Error(), "pending system") {
		t.Fatalf("application bypassed system journal: %v", err)
	}
}

func TestSystemCommitCannotDisableRollbackForWrongVersion(t *testing.T) {
	_, d, _ := fixture(t)
	s := SystemJournal{Schema: 1, Phase: "pending-reboot", Version: "99.0.0"}
	if err := validateSystemCommit(s, d); err == nil {
		t.Fatal("wrong running system could disable bootloader rollback")
	}
	s.Version = d.Runtime["system"]
	if err := validateSystemCommit(s, d); err != nil {
		t.Fatal(err)
	}
	s.Phase = "failed"
	if err := validateSystemCommit(s, d); err == nil {
		t.Fatal("failed transaction committed")
	}
}

func TestSystemCandidateCachePreservesExactSignedBytes(t *testing.T) {
	c, d, trust, _ := systemFixture(t)
	dir := t.TempDir()
	if err := atomicJSON(dir, "candidate.json", c); err != nil {
		t.Fatal(err)
	}
	var restored SystemCandidate
	if err := readSystemCandidate(filepath.Join(dir, "candidate.json"), &restored); err != nil {
		t.Fatal(err)
	}
	if string(c.Raw) != string(restored.Raw) || string(c.FactoryRaw) != string(restored.FactoryRaw) {
		t.Fatal("signed bytes changed in cache")
	}
	if _, err := restored.Verify(d, trust, "stable", 0); err != nil {
		t.Fatal(err)
	}
}

func TestSystemInterruptedWriteRecoversOnlyAfterAnotherBoot(t *testing.T) {
	e, _, _, _, _, _ := engineFixture(t)
	boot, err := os.ReadFile("/proc/sys/kernel/random/boot_id")
	if err != nil {
		t.Fatal(err)
	}
	state := SystemJournal{Schema: 1, Phase: "applying", HighWater: 23, Version: "99.0.0", PackageSHA256: strings.Repeat("a", 64), BootID: strings.TrimSpace(string(boot))}
	dir := filepath.Join(e.Root, "system")
	if err = atomicJSON(dir, "state.json", state); err != nil {
		t.Fatal(err)
	}
	if _, err = e.Select(); err != nil {
		t.Fatal(err)
	}
	got, err := systemJournal(dir)
	if err != nil || got.Phase != "applying" {
		t.Fatalf("same-boot transaction fence lost: %+v %v", got, err)
	}
	state.BootID = "previous-boot"
	if err = atomicJSON(dir, "state.json", state); err != nil {
		t.Fatal(err)
	}
	if _, err = e.Select(); err != nil {
		t.Fatal(err)
	}
	got, err = systemJournal(dir)
	if err != nil || got.Phase != "failed" || got.HighWater != 23 {
		t.Fatalf("rollback lost replay floor: %+v %v", got, err)
	}
}
