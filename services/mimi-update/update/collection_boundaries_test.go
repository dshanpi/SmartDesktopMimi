package update

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSelectCollectionPreservesRecoveryAndUserData(t *testing.T) {
	e, _, m, raw, sig, archive := engineFixture(t)
	if err := e.Install(archive, raw, sig, m.Channel); err != nil {
		t.Fatal(err)
	}
	releases := filepath.Join(e.Root, "releases")
	write := func(path string, b []byte) {
		t.Helper()
		if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, b, 0600); err != nil {
			t.Fatal(err)
		}
	}
	makeTree := func(version string) string {
		t.Helper()
		v := clone(m)
		v.Version = version
		v.Sequence = 1
		v, r, s, b := makePackage(t, v)
		p := filepath.Join(t.TempDir(), "archive")
		write(p, b)
		dir := e.dir(v.PackageSHA256)
		if err := os.MkdirAll(dir, 0700); err != nil {
			t.Fatal(err)
		}
		if err := CheckArchive(p, v, dir); err != nil {
			t.Fatal(err)
		}
		write(filepath.Join(dir, "manifest.json"), r)
		write(filepath.Join(dir, "manifest.sig"), s)
		return v.PackageSHA256
	}
	previous := makeTree("1.0.1")
	old := makeTree("0.9.0")
	state, err := e.readState()
	if err != nil {
		t.Fatal(err)
	}
	state.Previous = previous
	if err = e.writeState(state); err != nil {
		t.Fatal(err)
	}
	outside := filepath.Join(t.TempDir(), "user-data")
	write(outside, []byte("external-user-canary"))
	user := filepath.Join(e.Root, "user-data", "settings")
	write(user, []byte("settings-canary"))
	stage := filepath.Join(releases, ".stage-interrupted")
	write(filepath.Join(stage, "partial"), []byte("partial"))
	if err = os.Symlink(filepath.Dir(outside), filepath.Join(stage, "external")); err != nil {
		t.Fatal(err)
	}
	link := filepath.Join(releases, ".stage-link")
	if err = os.Symlink(filepath.Dir(outside), link); err != nil {
		t.Fatal(err)
	}
	unknown := filepath.Join(releases, "user-content", "canary")
	write(unknown, []byte("unknown"))
	unsigned := filepath.Join(releases, strings.Repeat("c", 64), "manifest.json")
	write(unsigned, []byte("unsigned"))
	mismatch := filepath.Join(releases, strings.Repeat("d", 64))
	write(filepath.Join(mismatch, "manifest.json"), raw)
	write(filepath.Join(mismatch, "manifest.sig"), sig)
	keep := []string{outside, user, unknown, unsigned, filepath.Join(mismatch, "manifest.json"), filepath.Join(e.Factory, "bin/lvglsim"), filepath.Join(e.dir(m.PackageSHA256), "bin/lvglsim"), filepath.Join(e.dir(previous), "bin/lvglsim"), filepath.Join(e.Root, "state.json")}
	before := map[string]string{}
	for _, p := range keep {
		b, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		before[p] = string(b)
	}
	for i := 0; i < 2; i++ {
		selected, err := e.Select()
		if err != nil || selected != e.dir(m.PackageSHA256) {
			t.Fatal(selected, err)
		}
		for _, p := range []string{e.dir(old), stage} {
			if _, err := os.Lstat(p); !os.IsNotExist(err) {
				t.Fatal("owned stale tree retained", p, err)
			}
		}
		for p, want := range before {
			b, err := os.ReadFile(p)
			if err != nil || string(b) != want {
				t.Fatal("protected bytes changed", p, err)
			}
		}
		if dest, err := os.Readlink(link); err != nil || dest != filepath.Dir(outside) {
			t.Fatal("symlink changed", dest, err)
		}
	}
}
