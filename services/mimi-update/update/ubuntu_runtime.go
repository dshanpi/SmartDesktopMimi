package update

import (
	"context"
	"debug/elf"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

type UbuntuLibrary struct {
	SONAME         string `json:"soname"`
	Path           string `json:"path"`
	SHA256         string `json:"sha256"`
	Package        string `json:"package"`
	PackageVersion string `json:"package_version"`
	RuntimeVersion string `json:"runtime_version"`
}

var ubuntuRuntimeRoots = []string{"libasound.so.2", "libjson-c.so.4", "libcurl.so.4", "libSDL2-2.0.so.0", "libpthread.so.0", "libstdc++.so.6", "libm.so.6", "libgcc_s.so.1", "libc.so.6", "libdl.so.2", "ld-linux-x86-64.so.2"}
var ubuntuLibDirs = []string{"/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu/pulseaudio"}
var sonamePattern = regexp.MustCompile(`^[A-Za-z0-9_+.-]+\.so(?:\.[A-Za-z0-9_+.-]+)*$`)
var ubuntuPackage = regexp.MustCompile(`^[a-z0-9][a-z0-9+.-]*(?::amd64)?$`)
var debUpstream = regexp.MustCompile(`^(?:[0-9]+:)?([0-9]+)(?:\.([0-9]+))?(?:\.([0-9]+))?`)
var debVersion = regexp.MustCompile(`^(?:[0-9]+:)?[0-9][A-Za-z0-9.+~-]*$`)

func ubuntuPackageVersion(raw string) (string, error) {
	m := debUpstream.FindStringSubmatch(raw)
	if m == nil || !debVersion.MatchString(raw) {
		return "", fmt.Errorf("unrecognized Debian runtime package version")
	}
	v := []string{"0", "0", "0"}
	for i := 0; i < 3; i++ {
		if m[i+1] != "" {
			n, e := strconv.ParseUint(m[i+1], 10, 64)
			if e != nil {
				return "", e
			}
			v[i] = strconv.FormatUint(n, 10)
		}
	}
	return strings.Join(v, "."), nil
}
func allowedUbuntuLibraryPath(p string) bool {
	for _, base := range ubuntuLibDirs {
		if filepath.Dir(p) == base {
			return true
		}
	}
	return false
}
func resolveUbuntuLibrary(name string) (string, error) {
	if !sonamePattern.MatchString(name) {
		return "", fmt.Errorf("invalid runtime SONAME")
	}
	var found string
	for _, base := range ubuntuLibDirs {
		path := filepath.Join(base, name)
		_, err := os.Lstat(path)
		if os.IsNotExist(err) {
			continue
		}
		if err != nil {
			return "", err
		}
		real, err := filepath.EvalSymlinks(path)
		if err != nil {
			return "", err
		}
		if !allowedUbuntuLibraryPath(real) {
			return "", fmt.Errorf("runtime library escapes fixed system paths")
		}
		if err = trustedFile(real); err != nil {
			return "", err
		}
		st, err := os.Stat(real)
		if err != nil {
			return "", err
		}
		if !st.Mode().IsRegular() || st.Size() < 1 || st.Size() > 128<<20 {
			return "", fmt.Errorf("invalid runtime library file")
		}
		if found != "" && found != real {
			return "", fmt.Errorf("ambiguous runtime library: %s", name)
		}
		found = real
	}
	if found == "" {
		return "", fmt.Errorf("missing runtime library: %s", name)
	}
	return found, nil
}
func ubuntuQuery(args ...string) ([]byte, error) {
	if err := trustedFile("/usr/bin/dpkg-query"); err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, "/usr/bin/dpkg-query", args...)
	cmd.Env = []string{"PATH=/usr/bin:/bin", "LC_ALL=C"}
	b, e := cmd.Output()
	if len(b) > 1<<20 {
		return nil, fmt.Errorf("dpkg output exceeds limit")
	}
	return b, e
}
func ubuntuOwners(raw []byte) (map[string]string, error) {
	out := map[string]string{}
	for _, line := range strings.Split(strings.TrimSpace(string(raw)), "\n") {
		if line == "" {
			continue
		}
		p := strings.SplitN(line, ": ", 2)
		if len(p) != 2 || !ubuntuPackage.MatchString(p[0]) || !allowedUbuntuLibraryPath(p[1]) {
			return nil, fmt.Errorf("unexpected runtime package ownership record")
		}
		if old, ok := out[p[1]]; ok && old != p[0] {
			return nil, fmt.Errorf("ambiguous runtime package owner")
		}
		out[p[1]] = p[0]
	}
	return out, nil
}

// InspectUbuntuRuntime never loads/executes the libraries. A fixed platform
// dependency graph is read as ELF metadata and every resolved file is pinned.
func InspectUbuntuRuntime() ([]UbuntuLibrary, error) {
	queue := append([]string{}, ubuntuRuntimeRoots...)
	seen := map[string]bool{}
	libs := []UbuntuLibrary{}
	for len(queue) > 0 {
		name := queue[0]
		queue = queue[1:]
		if seen[name] {
			continue
		}
		seen[name] = true
		if len(seen) > 256 {
			return nil, fmt.Errorf("runtime closure exceeds limit")
		}
		path, err := resolveUbuntuLibrary(name)
		if err != nil {
			return nil, err
		}
		f, err := elf.Open(path)
		if err != nil {
			return nil, err
		}
		if f.Machine != elf.EM_X86_64 || f.Class != elf.ELFCLASS64 || f.Data != elf.ELFDATA2LSB || f.Type != elf.ET_DYN {
			f.Close()
			return nil, fmt.Errorf("wrong runtime ELF architecture/type")
		}
		names, err := f.DynString(elf.DT_SONAME)
		if err != nil || len(names) != 1 || names[0] != name {
			f.Close()
			return nil, fmt.Errorf("runtime SONAME differs from selected name")
		}
		deps, err := f.ImportedLibraries()
		f.Close()
		if err != nil {
			return nil, err
		}
		queue = append(queue, deps...)
		hash, _, err := hashFile(path)
		if err != nil {
			return nil, err
		}
		libs = append(libs, UbuntuLibrary{SONAME: name, Path: path, SHA256: hash})
	}
	sort.Slice(libs, func(i, j int) bool { return libs[i].SONAME < libs[j].SONAME })
	paths := map[string]bool{}
	for _, lib := range libs {
		paths[lib.Path] = true
		if strings.HasPrefix(lib.Path, "/usr/lib/") {
			paths[strings.TrimPrefix(lib.Path, "/usr")] = true
		} else {
			paths["/usr"+lib.Path] = true
		}
	}
	args := []string{"--search"}
	for p := range paths {
		args = append(args, p)
	}
	sort.Strings(args[1:])
	raw, err := ubuntuQuery(args...)
	// dpkg returns 1 when an alternate merged-/usr spelling has no owner. All
	// actual files must still resolve to one installed package below.
	if err != nil {
		if x, ok := err.(*exec.ExitError); !ok || x.ExitCode() != 1 {
			return nil, err
		}
	}
	owners, err := ubuntuOwners(raw)
	if err != nil {
		return nil, err
	}
	packages := map[string]bool{}
	for i := range libs {
		p := libs[i].Path
		alias := "/usr" + p
		if strings.HasPrefix(p, "/usr/lib/") {
			alias = strings.TrimPrefix(p, "/usr")
		}
		owner := owners[p]
		other := owners[alias]
		if owner == "" {
			owner = other
		} else if other != "" && owner != other {
			return nil, fmt.Errorf("merged-/usr library ownership differs")
		}
		if owner == "" {
			return nil, fmt.Errorf("runtime library has no installed package: %s", libs[i].SONAME)
		}
		libs[i].Package = owner
		packages[owner] = true
	}
	args = []string{"--show", "--showformat=${binary:Package}\t${Version}\t${db:Status-Status}\n"}
	for p := range packages {
		args = append(args, p)
	}
	sort.Strings(args[2:])
	raw, err = ubuntuQuery(args...)
	if err != nil {
		return nil, err
	}
	versions := map[string]string{}
	for _, line := range strings.Split(strings.TrimSpace(string(raw)), "\n") {
		p := strings.Split(line, "\t")
		if len(p) != 3 || p[2] != "installed" || !ubuntuPackage.MatchString(p[0]) {
			return nil, fmt.Errorf("runtime package is not installed")
		}
		if _, ok := versions[p[0]]; ok {
			return nil, fmt.Errorf("duplicate runtime package version")
		}
		versions[p[0]] = p[1]
	}
	for i := range libs {
		lib := &libs[i]
		lib.PackageVersion = versions[lib.Package]
		lib.RuntimeVersion, err = ubuntuPackageVersion(lib.PackageVersion)
		if err != nil {
			return nil, err
		}
		// Detect concurrent package replacement during inspection.
		p, err := resolveUbuntuLibrary(lib.SONAME)
		if err != nil || p != lib.Path {
			return nil, fmt.Errorf("runtime path changed during inspection")
		}
		h, _, err := hashFile(p)
		if err != nil || h != lib.SHA256 {
			return nil, fmt.Errorf("runtime bytes changed during inspection")
		}
	}
	confirmed, err := ubuntuQuery(args...)
	if err != nil || string(confirmed) != string(raw) {
		return nil, fmt.Errorf("runtime package metadata changed during inspection")
	}
	return libs, nil
}
func bindUbuntuRuntime(d *Device, libs []UbuntuLibrary) error {
	if len(libs) == 0 {
		return fmt.Errorf("empty Ubuntu runtime inventory")
	}
	seen := map[string]bool{}
	for _, lib := range libs {
		if seen[lib.SONAME] || !sonamePattern.MatchString(lib.SONAME) || !sha256Pattern.MatchString(lib.SHA256) || !allowedUbuntuLibraryPath(lib.Path) || !ubuntuPackage.MatchString(lib.Package) {
			return fmt.Errorf("invalid Ubuntu runtime inventory")
		}
		seen[lib.SONAME] = true
		v, err := ubuntuPackageVersion(lib.PackageVersion)
		if err != nil || v != lib.RuntimeVersion {
			return fmt.Errorf("runtime package/version mismatch")
		}
		d.Runtime["soname:"+lib.SONAME] = v
	}
	for _, required := range ubuntuRuntimeRoots {
		if !seen[required] {
			return fmt.Errorf("missing required runtime SONAME")
		}
	}
	raw, err := json.Marshal(libs)
	if err != nil {
		return err
	}
	d.Capabilities = append(d.Capabilities, "runtime-bundle-sha256:"+bytesDigest(raw))
	return d.Validate()
}
