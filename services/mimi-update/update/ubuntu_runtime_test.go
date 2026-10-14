package update

import (
	"strings"
	"testing"
)

func TestUbuntuDebVersionPreservesNumericComponents(t *testing.T) {
	for raw, want := range map[string]string{"1.1.1f-1ubuntu2.24": "1.1.1", "2:1.6.9-2ubuntu1.6": "1.6.9", "16-20260315-1ubuntu1~20~ppa1": "16.0.0", "2.31-0ubuntu9.18": "2.31.0", "1:13.99.1-1ubuntu3.13": "13.99.1", "7.6.q-30": "7.6.0", "1.2.11.dfsg-2": "1.2.11", "1.02.003-1": "1.2.3"} {
		got, e := ubuntuPackageVersion(raw)
		if e != nil || got != want {
			t.Fatalf("%s: %s %v", raw, got, e)
		}
	}
	for _, raw := range []string{"", "unknown", "1\n2", "1 2", "1/2", "1:2:3", "999999999999999999999999999.0"} {
		if _, e := ubuntuPackageVersion(raw); e == nil {
			t.Fatalf("accepted %q", raw)
		}
	}
}
func TestUbuntuRuntimeOwnershipAndPathLimits(t *testing.T) {
	for _, p := range []string{"/tmp/lib.so", "/usr/lib/x86_64-linux-gnu/../../tmp/lib.so", "/usr/local/lib/lib.so", "/usr/lib/x86_64-linux-gnu/plugin/lib.so"} {
		if allowedUbuntuLibraryPath(p) {
			t.Fatalf("accepted %s", p)
		}
	}
	for _, name := range []string{"../libc.so.6", "/lib/libc.so.6", "libc.so.6\n", "libc.so.6;id"} {
		if _, e := resolveUbuntuLibrary(name); e == nil {
			t.Fatal("unsafe library accepted")
		}
	}
	good := []byte("libc6:amd64: /lib/x86_64-linux-gnu/libc-2.31.so\n")
	if _, e := ubuntuOwners(good); e != nil {
		t.Fatal(e)
	}
	for _, raw := range []string{"libc6:i386: /lib/x86_64-linux-gnu/libc-2.31.so", "diversion by x", "libc6:amd64: /tmp/libc.so", string(good) + "other: /lib/x86_64-linux-gnu/libc-2.31.so"} {
		if _, e := ubuntuOwners([]byte(raw)); e == nil {
			t.Fatal("unsafe owner accepted")
		}
	}
}
func runtimeFixture() []UbuntuLibrary {
	out := []UbuntuLibrary{}
	for _, name := range ubuntuRuntimeRoots {
		out = append(out, UbuntuLibrary{SONAME: name, Path: "/lib/x86_64-linux-gnu/" + name, SHA256: strings.Repeat("a", 64), Package: "test:amd64", PackageVersion: "1.1.1f-1", RuntimeVersion: "1.1.1"})
	}
	return out
}
func TestUbuntuRuntimeFingerprintBindsBytesAndFullVersion(t *testing.T) {
	original, _, _ := ubuntuFixture(t)
	libs := runtimeFixture()
	first := clone(original)
	if e := bindUbuntuRuntime(&first, libs); e != nil {
		t.Fatal(e)
	}
	for name, mutate := range map[string]func([]UbuntuLibrary){"bytes": func(l []UbuntuLibrary) { l[0].SHA256 = strings.Repeat("b", 64) }, "package revision": func(l []UbuntuLibrary) { l[0].PackageVersion = "1.1.1f-2" }, "resolved path": func(l []UbuntuLibrary) { l[0].Path = "/usr/lib/x86_64-linux-gnu/" + l[0].SONAME }} {
		t.Run(name, func(t *testing.T) {
			modified := clone(libs)
			mutate(modified)
			d := clone(original)
			if e := bindUbuntuRuntime(&d, modified); e != nil {
				t.Fatal(e)
			}
			if d.Capabilities[len(d.Capabilities)-1] == first.Capabilities[len(first.Capabilities)-1] {
				t.Fatal("change did not alter compatibility fingerprint")
			}
		})
	}
	for name, mutate := range map[string]func([]UbuntuLibrary){"duplicate": func(l []UbuntuLibrary) { l[1] = l[0] }, "version mismatch": func(l []UbuntuLibrary) { l[0].RuntimeVersion = "1.1.0" }, "unowned path": func(l []UbuntuLibrary) { l[0].Path = "/tmp/lib.so" }, "hash": func(l []UbuntuLibrary) { l[0].SHA256 = "wrong" }} {
		t.Run(name, func(t *testing.T) {
			modified := clone(libs)
			mutate(modified)
			d := clone(original)
			if e := bindUbuntuRuntime(&d, modified); e == nil {
				t.Fatal("invalid inventory accepted")
			}
		})
	}
	d := clone(original)
	if e := bindUbuntuRuntime(&d, libs[:len(libs)-1]); e == nil {
		t.Fatal("missing runtime accepted")
	}
}
