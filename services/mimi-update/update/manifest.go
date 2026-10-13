// Package update implements the shared MIMI-UPDATE-001 admission gate.
package update

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"path"
	"regexp"
	"strconv"
	"strings"
)

const Version = "1.0.0"
const Policy = "MIMI-UPDATE-001/1.0.0"

var identityKeys = []string{"product_id", "platform_id", "soc_id", "board_id", "hardware_revision", "variant", "architecture", "isa", "bits", "endian", "abi", "libc", "loader", "ports_abi", "ipc_contract"}
var runtimeKeys = []string{"system", "provider", "updater", "libc"}
var token = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._+:/-]{0,191}$`)
var sha256Pattern = regexp.MustCompile(`^[a-f0-9]{64}$`)
var commitPattern = regexp.MustCompile(`^[a-f0-9]{40}$`)

type Range struct {
	Min          string   `json:"min"`
	MaxExclusive string   `json:"max_exclusive"`
	Exclude      []string `json:"exclude"`
}
type Target struct {
	Identity     map[string]string `json:"identity"`
	Runtime      map[string]Range  `json:"runtime"`
	Capabilities []string          `json:"capabilities"`
}
type Device struct {
	Schema         int               `json:"schema_version"`
	Identity       map[string]string `json:"identity"`
	Runtime        map[string]string `json:"runtime"`
	Capabilities   []string          `json:"capabilities"`
	DataSchema     int               `json:"data_schema"`
	SystemBaseline string            `json:"system_baseline"`
	// Provisioning must bind this file to independently read board evidence.
	Enrollment string `json:"enrollment_id"`
}
type DataRange struct {
	Min   int `json:"min"`
	Max   int `json:"max"`
	Write int `json:"write"`
}
type File struct {
	SHA256 string `json:"sha256"`
	Size   int64  `json:"size"`
	Mode   uint32 `json:"mode"`
}
type Manifest struct {
	Schema        int             `json:"schema_version"`
	Policy        string          `json:"policy"`
	Type          string          `json:"release_type"`
	Version       string          `json:"version"`
	Commit        string          `json:"commit"`
	Channel       string          `json:"channel"`
	Sequence      uint64          `json:"sequence"`
	KeyID         string          `json:"key_id"`
	PackageSHA256 string          `json:"package_sha256"`
	PackageBytes  int64           `json:"package_bytes"`
	ExpandedBytes int64           `json:"expanded_bytes"`
	MaxFiles      int             `json:"max_files"`
	Files         map[string]File `json:"files"`
	Targets       []Target        `json:"targets"`
	Data          DataRange       `json:"data"`
}

// StrictJSON also rejects duplicate keys; encoding/json alone accepts them.
func StrictJSON(raw []byte, out any) error {
	return strictJSONLimit(raw, out, 1<<20)
}

func strictJSONLimit(raw []byte, out any, limit int) error {
	if len(raw) == 0 || len(raw) > limit {
		return fmt.Errorf("JSON size outside limit")
	}
	d := json.NewDecoder(bytes.NewReader(raw))
	var walk func() error
	walk = func() error {
		t, e := d.Token()
		if e != nil {
			return e
		}
		if v, ok := t.(json.Delim); ok {
			switch v {
			case '{':
				seen := map[string]bool{}
				for d.More() {
					k, e := d.Token()
					if e != nil {
						return e
					}
					s, ok := k.(string)
					if !ok || seen[s] {
						return fmt.Errorf("duplicate/invalid JSON key %v", k)
					}
					seen[s] = true
					if e = walk(); e != nil {
						return e
					}
				}
			case '[':
				for d.More() {
					if e := walk(); e != nil {
						return e
					}
				}
			default:
				return fmt.Errorf("unexpected delimiter")
			}
			_, e = d.Token()
			return e
		}
		return nil
	}
	if e := walk(); e != nil {
		return e
	}
	if _, e := d.Token(); e != io.EOF {
		return fmt.Errorf("trailing JSON")
	}
	d = json.NewDecoder(bytes.NewReader(raw))
	d.DisallowUnknownFields()
	return d.Decode(out)
}

type semver struct {
	n   [3]uint64
	pre []string
}

var versionPattern = regexp.MustCompile(`^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$`)

func parseVersion(s string) (semver, error) {
	var v semver
	m := versionPattern.FindStringSubmatch(s)
	if m == nil {
		return v, fmt.Errorf("invalid SemVer %q", s)
	}
	for i := 0; i < 3; i++ {
		n, e := strconv.ParseUint(m[i+1], 10, 64)
		if e != nil {
			return v, e
		}
		v.n[i] = n
	}
	if m[4] != "" {
		v.pre = strings.Split(m[4], ".")
		for _, p := range v.pre {
			if numeric(p) && len(p) > 1 && p[0] == '0' {
				return v, fmt.Errorf("leading zero in prerelease")
			}
		}
	}
	return v, nil
}
func numeric(s string) bool {
	for _, c := range s {
		if c < '0' || c > '9' {
			return false
		}
	}
	return s != ""
}
func Compare(a, b string) (int, error) {
	x, e := parseVersion(a)
	if e != nil {
		return 0, e
	}
	y, e := parseVersion(b)
	if e != nil {
		return 0, e
	}
	for i := 0; i < 3; i++ {
		if x.n[i] < y.n[i] {
			return -1, nil
		}
		if x.n[i] > y.n[i] {
			return 1, nil
		}
	}
	if len(x.pre) == 0 && len(y.pre) > 0 {
		return 1, nil
	}
	if len(y.pre) == 0 && len(x.pre) > 0 {
		return -1, nil
	}
	for i := 0; i < len(x.pre) && i < len(y.pre); i++ {
		a, b := x.pre[i], y.pre[i]
		if a == b {
			continue
		}
		an, bn := numeric(a), numeric(b)
		if an && bn {
			if len(a) < len(b) {
				return -1, nil
			}
			if len(a) > len(b) {
				return 1, nil
			}
		} else if an != bn {
			if an {
				return -1, nil
			}
			return 1, nil
		}
		if a < b {
			return -1, nil
		}
		return 1, nil
	}
	if len(x.pre) < len(y.pre) {
		return -1, nil
	}
	if len(x.pre) > len(y.pre) {
		return 1, nil
	}
	return 0, nil
}
func (r Range) Validate() error {
	c, e := Compare(r.Min, r.MaxExclusive)
	if e != nil {
		return e
	}
	if c >= 0 {
		return fmt.Errorf("empty version range")
	}
	for _, v := range r.Exclude {
		if _, e := parseVersion(v); e != nil {
			return e
		}
	}
	return nil
}
func (r Range) Contains(v string) bool {
	a, e := Compare(v, r.Min)
	if e != nil {
		return false
	}
	b, e := Compare(v, r.MaxExclusive)
	if e != nil || a < 0 || b >= 0 {
		return false
	}
	for _, x := range r.Exclude {
		c, e := Compare(v, x)
		if e != nil || c == 0 {
			return false
		}
	}
	return true
}
func validateIdentity(m map[string]string) error {
	if len(m) != len(identityKeys) {
		return fmt.Errorf("identity has missing or unknown fields")
	}
	for _, k := range identityKeys {
		v := m[k]
		valid := token.MatchString(v)
		if k == "loader" {
			valid = (strings.HasPrefix(v, "/lib/") || strings.HasPrefix(v, "/lib64/")) && path.Clean(v) == v && token.MatchString(strings.TrimPrefix(v, "/"))
		}
		if !valid || strings.Contains(v, "*") || v == "unknown" {
			return fmt.Errorf("invalid identity.%s", k)
		}
	}
	return nil
}
func validateCaps(c []string) error {
	seen := map[string]bool{}
	for _, s := range c {
		if !token.MatchString(s) || seen[s] {
			return fmt.Errorf("invalid/duplicate capability")
		}
		seen[s] = true
	}
	return nil
}
func (d Device) Validate() error {
	if d.Schema != 1 || d.Enrollment == "" || !token.MatchString(d.SystemBaseline) || d.DataSchema < 1 {
		return fmt.Errorf("device identity is not enrolled or baseline is missing")
	}
	if e := validateIdentity(d.Identity); e != nil {
		return e
	}
	for _, k := range runtimeKeys {
		if _, e := parseVersion(d.Runtime[k]); e != nil {
			return fmt.Errorf("runtime.%s: %w", k, e)
		}
	}
	for k, v := range d.Runtime {
		if !token.MatchString(k) {
			return fmt.Errorf("invalid runtime name")
		}
		if _, e := parseVersion(v); e != nil {
			return e
		}
	}
	return validateCaps(d.Capabilities)
}
func (m Manifest) Validate() error {
	if m.Schema != 1 || m.Policy != Policy || m.Type != "application" {
		return fmt.Errorf("unsupported manifest/policy/release type")
	}
	v, e := parseVersion(m.Version)
	if e != nil {
		return e
	}
	if m.Channel != "stable" && m.Channel != "testing" {
		return fmt.Errorf("unknown channel")
	}
	if m.Channel == "stable" && len(v.pre) > 0 {
		return fmt.Errorf("prerelease in stable channel")
	}
	if m.Sequence == 0 || !token.MatchString(m.KeyID) || strings.ContainsAny(m.KeyID, "/:") || !commitPattern.MatchString(m.Commit) || !sha256Pattern.MatchString(m.PackageSHA256) {
		return fmt.Errorf("invalid release identity/hash")
	}
	if m.PackageBytes < 1 || m.PackageBytes > 256<<20 || m.ExpandedBytes < 1 || m.ExpandedBytes > 512<<20 || m.MaxFiles < 1 || m.MaxFiles > 20000 {
		return fmt.Errorf("package limits outside format bounds")
	}
	if m.Data.Min < 1 || m.Data.Max < m.Data.Min || m.Data.Write < m.Data.Min || m.Data.Write > m.Data.Max {
		return fmt.Errorf("invalid data compatibility")
	}
	if len(m.Files) == 0 || len(m.Files) > m.MaxFiles {
		return fmt.Errorf("invalid file inventory")
	}
	var total int64
	for name, f := range m.Files {
		if !allowedPath(name) || !sha256Pattern.MatchString(f.SHA256) || f.Size < 0 || f.Size > m.ExpandedBytes || (f.Mode != 0644 && f.Mode != 0755) {
			return fmt.Errorf("invalid file %q", name)
		}
		total += f.Size
	}
	if total != m.ExpandedBytes {
		return fmt.Errorf("expanded size does not match inventory")
	}
	if _, ok := m.Files["bin/lvglsim"]; !ok {
		return fmt.Errorf("missing desktop executable")
	}
	return validateTargets(m.Targets)
}

func validateTargets(targets []Target) error {
	if len(targets) == 0 || len(targets) > 64 {
		return fmt.Errorf("empty/excessive targets")
	}
	for _, t := range targets {
		if e := validateIdentity(t.Identity); e != nil {
			return e
		}
		for _, k := range runtimeKeys {
			if _, ok := t.Runtime[k]; !ok {
				return fmt.Errorf("missing runtime.%s", k)
			}
		}
		for k, r := range t.Runtime {
			if !token.MatchString(k) {
				return fmt.Errorf("invalid runtime name")
			}
			if e := r.Validate(); e != nil {
				return fmt.Errorf("runtime.%s: %w", k, e)
			}
		}
		if e := validateCaps(t.Capabilities); e != nil {
			return e
		}
	}
	return nil
}
func Match(m Manifest, d Device) error {
	if e := m.Validate(); e != nil {
		return e
	}
	if e := d.Validate(); e != nil {
		return e
	}
	// v1 deliberately supports no irreversible data migrations.
	if d.DataSchema < m.Data.Min || d.DataSchema > m.Data.Max || m.Data.Write != d.DataSchema {
		return fmt.Errorf("data schema requires an unsupported migration")
	}
	return matchTargets(m.Targets, d)
}

func matchTargets(targets []Target, d Device) error {
	caps := map[string]bool{}
	for _, c := range d.Capabilities {
		caps[c] = true
	}
	failures := []string{}
	for i, t := range targets {
		reason := ""
		for _, k := range identityKeys {
			if t.Identity[k] != d.Identity[k] {
				reason = fmt.Sprintf("%s: actual=%q required=%q", k, d.Identity[k], t.Identity[k])
				break
			}
		}
		if reason == "" {
			for k, r := range t.Runtime {
				if !r.Contains(d.Runtime[k]) {
					reason = fmt.Sprintf("runtime.%s: actual=%q required=[%s,%s)", k, d.Runtime[k], r.Min, r.MaxExclusive)
					break
				}
			}
		}
		if reason == "" {
			for _, c := range t.Capabilities {
				if !caps[c] {
					reason = "missing capability " + c
					break
				}
			}
		}
		if reason == "" {
			return nil
		}
		failures = append(failures, fmt.Sprintf("target[%d] %s", i, reason))
	}
	return fmt.Errorf("incompatible: %s", strings.Join(failures, "; "))
}
