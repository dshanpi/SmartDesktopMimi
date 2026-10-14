package update

import (
	"strings"
	"testing"
)

func ubuntuFixture(t *testing.T) (Device, Enrollment, map[string][]byte) {
	t.Helper()
	nodes := map[string][]byte{"board_name": []byte("440BX Desktop Reference Platform\n"), "board_version": []byte("None\n"), "product_name": []byte("VMware Virtual Platform\n"), "sys_vendor": []byte("VMware, Inc.\n"), "product_uuid": []byte("12345678-1234-1234-1234-123456789abc\n")}
	d, e, err := ubuntuRecords("ubuntu-host-20.04-dev-20261014", "x86_64", "glibc 2.31", []byte("ID=ubuntu\nVERSION_ID=\"20.04\"\n"), nodes)
	if err != nil {
		t.Fatal(err)
	}
	return d, e, nodes
}
func TestUbuntuRecordsRequireActualTarget(t *testing.T) {
	_, _, nodes := ubuntuFixture(t)
	cases := []struct {
		name, arch, libc, os string
		node, value          string
	}{
		{"architecture", "aarch64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04", "", ""},
		{"OS", "x86_64", "glibc 2.31", "ID=debian\nVERSION_ID=20.04", "", ""},
		{"OS version", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=22.04", "", ""},
		{"libc", "x86_64", "glibc 2.35", "ID=ubuntu\nVERSION_ID=20.04", "", ""},
		{"duplicate OS", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04\nID=ubuntu", "", ""},
		{"missing node", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04", "board_version", ""},
		{"wrong vendor", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04", "sys_vendor", "other"},
		{"wrong product", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04", "product_name", "other"},
		{"invalid uuid", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04", "product_uuid", "none"},
		{"zero uuid", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04", "product_uuid", "00000000-0000-0000-0000-000000000000"},
		{"ff uuid", "x86_64", "glibc 2.31", "ID=ubuntu\nVERSION_ID=20.04", "product_uuid", "ffffffff-ffff-ffff-ffff-ffffffffffff"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			n := clone(nodes)
			if c.node != "" {
				n[c.node] = []byte(c.value)
			}
			if _, _, err := ubuntuRecords("baseline", c.arch, c.libc, []byte(c.os), n); err == nil {
				t.Fatal("unsupported evidence accepted")
			}
		})
	}
	if _, _, err := ubuntuRecords("", "x86_64", "glibc 2.31", []byte("ID=ubuntu\nVERSION_ID=20.04"), nodes); err == nil {
		t.Fatal("missing baseline accepted")
	}
}
func TestUbuntuEnrollmentExactHardwareAndEvidence(t *testing.T) {
	d, e, _ := ubuntuFixture(t)
	if err := compareUbuntuEnrollment(d, e, d, e); err != nil {
		t.Fatal(err)
	}
	reversed := clone(e)
	for i, j := 0, len(reversed.Evidence)-1; i < j; i, j = i+1, j-1 {
		reversed.Evidence[i], reversed.Evidence[j] = reversed.Evidence[j], reversed.Evidence[i]
	}
	if err := compareUbuntuEnrollment(d, reversed, d, e); err != nil {
		t.Fatal(err)
	}
	for _, field := range identityKeys {
		t.Run(field, func(t *testing.T) {
			bad := clone(d)
			bad.Identity[field] = "other"
			if err := compareUbuntuEnrollment(bad, e, d, e); err == nil {
				t.Fatal("wrong identity accepted")
			}
		})
	}
	for _, field := range runtimeKeys {
		t.Run(field, func(t *testing.T) {
			bad := clone(d)
			bad.Runtime[field] = "99.0.0"
			if err := compareUbuntuEnrollment(bad, e, d, e); err == nil {
				t.Fatal("wrong runtime accepted")
			}
		})
	}
	for name, mutate := range map[string]func(*Enrollment){
		"path":      func(x *Enrollment) { x.Evidence[0].Path = "/tmp/board_name" },
		"hash":      func(x *Enrollment) { x.Evidence[0].SHA256 = strings.Repeat("a", 64) },
		"kind":      func(x *Enrollment) { x.Evidence[0].Kind = "silicon" },
		"missing":   func(x *Enrollment) { x.Evidence = x.Evidence[:4] },
		"duplicate": func(x *Enrollment) { x.Evidence[1] = x.Evidence[0] },
		"ID":        func(x *Enrollment) { x.EnrollmentID = "other" },
		"digest":    func(x *Enrollment) { x.IdentitySHA256 = strings.Repeat("b", 64) },
		"schema":    func(x *Enrollment) { x.Schema = 2 },
	} {
		t.Run(name, func(t *testing.T) {
			bad := clone(e)
			mutate(&bad)
			if err := compareUbuntuEnrollment(d, bad, d, e); err == nil {
				t.Fatal("bad enrollment accepted")
			}
		})
	}
	for name, mutate := range map[string]func(*Device){"baseline": func(x *Device) { x.SystemBaseline = "other" }, "data": func(x *Device) { x.DataSchema = 2 }, "caps": func(x *Device) { x.Capabilities = append(x.Capabilities, "hdmi") }} {
		t.Run(name, func(t *testing.T) {
			bad := clone(d)
			mutate(&bad)
			if err := compareUbuntuEnrollment(bad, e, d, e); err == nil {
				t.Fatal("bad device profile accepted")
			}
		})
	}
}
func TestUbuntuEnrollmentDoesNotClaimSiliconOrExposeUUID(t *testing.T) {
	d, e, n := ubuntuFixture(t)
	if strings.Contains(d.Enrollment, strings.TrimSpace(string(n["product_uuid"]))) {
		t.Fatal("plaintext UUID")
	}
	for _, p := range e.Evidence {
		if p.Kind == "silicon" {
			t.Fatal("VM treated as silicon")
		}
	}
	if !strings.HasPrefix(d.Identity["hardware_revision"], "dmi-") {
		t.Fatal("fabricated PCB revision")
	}
	if allowedEnrollmentEvidencePath("a133", "silicon", "/tmp/product_uuid") {
		t.Fatal("A133 path broadened")
	}
}
