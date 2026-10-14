package update

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"
)

// This first Ubuntu target is the actual VMware development host, not A133
// silicon or a claim of qualification for arbitrary Debian/Ubuntu hardware.
var ubuntuDMI = []struct{ kind, node string }{
	{"board", "board_name"}, {"revision", "board_version"},
	{"product", "product_name"}, {"vendor", "sys_vendor"}, {"machine", "product_uuid"},
}
var ubuntuUUID = regexp.MustCompile(`(?i)^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$`)

func bytesDigest(b []byte) string { h := sha256.Sum256(b); return hex.EncodeToString(h[:]) }

func ubuntuOS(raw []byte) (map[string]string, error) {
	values := map[string]string{}
	for _, line := range strings.Split(string(raw), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		p := strings.SplitN(line, "=", 2)
		if len(p) != 2 {
			return nil, fmt.Errorf("invalid Ubuntu OS release")
		}
		if _, ok := values[p[0]]; ok {
			return nil, fmt.Errorf("duplicate Ubuntu OS release field")
		}
		v := p[1]
		if strings.HasPrefix(v, `"`) {
			var e error
			v, e = strconv.Unquote(v)
			if e != nil {
				return nil, e
			}
		}
		values[p[0]] = v
	}
	if values["ID"] != "ubuntu" || values["VERSION_ID"] != "20.04" {
		return nil, fmt.Errorf("unqualified Ubuntu OS target")
	}
	return values, nil
}

func ubuntuRecords(baseline, machine, libc string, osRelease []byte, nodes map[string][]byte) (Device, Enrollment, error) {
	var d Device
	var e Enrollment
	if !token.MatchString(baseline) || machine != "x86_64" || libc != "glibc 2.31" {
		return d, e, fmt.Errorf("unsupported Ubuntu baseline/architecture/libc")
	}
	if _, err := ubuntuOS(osRelease); err != nil {
		return d, e, err
	}
	for _, field := range ubuntuDMI {
		b := nodes[field.node]
		if len(b) == 0 || len(b) > 4096 || len(strings.TrimSpace(string(b))) == 0 {
			return d, e, fmt.Errorf("missing/oversize DMI evidence: %s", field.kind)
		}
	}
	if strings.TrimSpace(string(nodes["sys_vendor"])) != "VMware, Inc." || strings.TrimSpace(string(nodes["product_name"])) != "VMware Virtual Platform" {
		return d, e, fmt.Errorf("Ubuntu DMI target is not qualified by this development profile")
	}
	uuid := strings.ToLower(strings.TrimSpace(string(nodes["product_uuid"])))
	flat := strings.ReplaceAll(uuid, "-", "")
	if !ubuntuUUID.MatchString(uuid) || flat == strings.Repeat("0", 32) || flat == strings.Repeat("f", 32) {
		return d, e, fmt.Errorf("missing/invalid DMI machine identity")
	}
	d = Device{Schema: 1, Identity: map[string]string{
		"product_id": "smartdesktopmimi", "platform_id": "ubuntu-host", "soc_id": "virtual-x86_64",
		"board_id": "dmi-" + bytesDigest(nodes["board_name"]), "hardware_revision": "dmi-" + bytesDigest(nodes["board_version"]),
		"variant": "dmi-" + bytesDigest(nodes["product_name"]), "architecture": "x86_64", "isa": "x86-64", "bits": "64", "endian": "little",
		"abi": "x86_64-linux-gnu", "libc": "glibc", "loader": "/lib64/ld-linux-x86-64.so.2", "ports_abi": "2", "ipc_contract": "mimi-ipc-v1.1",
	}, Runtime: map[string]string{"system": "20.4.0", "provider": "0.2.0", "updater": Version, "libc": "2.31.0"}, Capabilities: []string{"target-os:ubuntu:20.04"}, DataSchema: 1, SystemBaseline: baseline, Enrollment: "ubuntu-" + bytesDigest(nodes["product_uuid"])}
	if err := d.Validate(); err != nil {
		return Device{}, Enrollment{}, err
	}
	hardware := map[string]string{}
	for _, k := range identityKeys[:7] {
		hardware[k] = d.Identity[k]
	}
	raw, err := json.Marshal(hardware)
	if err != nil {
		return Device{}, Enrollment{}, err
	}
	e = Enrollment{Schema: 1, EnrollmentID: d.Enrollment, IdentitySHA256: bytesDigest(raw)}
	for _, field := range ubuntuDMI {
		e.Evidence = append(e.Evidence, EnrollmentEvidence{field.kind, "/sys/devices/virtual/dmi/id/" + field.node, bytesDigest(nodes[field.node])})
	}
	return d, e, nil
}

// InspectUbuntu reads only fixed kernel and OS sources. No arbitrary evidence
// path, signing key, machine UUID plaintext, or installation write is exposed.
func InspectUbuntu(baseline string) (Device, Enrollment, error) {
	var d Device
	var enrollment Enrollment
	if runtime.GOOS != "linux" || runtime.GOARCH != "amd64" {
		return d, enrollment, fmt.Errorf("Ubuntu inspector requires native Linux amd64")
	}
	var uts syscall.Utsname
	if err := syscall.Uname(&uts); err != nil {
		return d, enrollment, err
	}
	name := []byte{}
	for _, c := range uts.Machine {
		if c == 0 {
			break
		}
		name = append(name, byte(c))
	}
	nodes := map[string][]byte{}
	for _, field := range ubuntuDMI {
		p := "/sys/devices/virtual/dmi/id/" + field.node
		real, err := filepath.EvalSymlinks(p)
		if err != nil {
			return d, enrollment, err
		}
		if real != p {
			return d, enrollment, fmt.Errorf("unexpected DMI evidence alias")
		}
		b, err := os.ReadFile(p)
		if err != nil {
			return d, enrollment, err
		}
		nodes[field.node] = b
	}
	if err := trustedFile("/usr/lib/os-release"); err != nil {
		return d, enrollment, err
	}
	osRelease, err := os.ReadFile("/usr/lib/os-release")
	if err != nil {
		return d, enrollment, err
	}
	if err = trustedFile("/usr/bin/getconf"); err != nil {
		return d, enrollment, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, "/usr/bin/getconf", "GNU_LIBC_VERSION")
	cmd.Env = []string{"PATH=/usr/bin:/bin", "LC_ALL=C"}
	libc, err := cmd.Output()
	if err != nil {
		return d, enrollment, fmt.Errorf("cannot read actual libc: %w", err)
	}
	loader, err := filepath.EvalSymlinks("/lib64/ld-linux-x86-64.so.2")
	if err != nil {
		return d, enrollment, err
	}
	if err = trustedFile(loader); err != nil {
		return d, enrollment, err
	}
	d, enrollment, err = ubuntuRecords(baseline, string(name), strings.TrimSpace(string(libc)), osRelease, nodes)
	if err != nil {
		return d, enrollment, err
	}
	libs, err := InspectUbuntuRuntime()
	if err != nil {
		return d, enrollment, err
	}
	err = bindUbuntuRuntime(&d, libs)
	return d, enrollment, err
}

func compareUbuntuEnrollment(d Device, actual Enrollment, expectedDevice Device, expected Enrollment) error {
	if d.Enrollment != "" && d.Enrollment != actual.EnrollmentID {
		return fmt.Errorf("device enrollment reference differs")
	}
	d.Enrollment = actual.EnrollmentID
	if err := d.Validate(); err != nil {
		return err
	}
	// Runtime Provider version is the controlled profile contract; session health
	// must independently prove the live Provider handshake before activation.
	if !reflect.DeepEqual(d, expectedDevice) {
		return fmt.Errorf("Ubuntu live hardware/OS/runtime profile differs from enrollment")
	}
	if actual.Schema != expected.Schema || actual.EnrollmentID != expected.EnrollmentID || actual.IdentitySHA256 != expected.IdentitySHA256 || len(actual.Evidence) != len(expected.Evidence) {
		return fmt.Errorf("Ubuntu enrollment identity/evidence differs")
	}
	seen := map[string]bool{}
	for _, proof := range actual.Evidence {
		if seen[proof.Kind] {
			return fmt.Errorf("duplicate Ubuntu enrollment evidence")
		}
		seen[proof.Kind] = true
		found := false
		for _, want := range expected.Evidence {
			if proof == want {
				found = true
				break
			}
		}
		if !found {
			return fmt.Errorf("Ubuntu kernel evidence differs: %s", proof.Kind)
		}
	}
	return nil
}

func validateUbuntuEnrollment(d Device, e Enrollment) error {
	expectedDevice, expected, err := InspectUbuntu(d.SystemBaseline)
	if err != nil {
		return err
	}
	return compareUbuntuEnrollment(d, e, expectedDevice, expected)
}
