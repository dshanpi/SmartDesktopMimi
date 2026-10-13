package update

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
)

// ReleaseIndex binds all platform artifacts to one application source/version.
// Each referenced manifest is independently signed and checked by Match.
type ReleaseIndex struct {
	Schema     int              `json:"schema_version"`
	Policy     string           `json:"policy"`
	Type       string           `json:"release_type"`
	Repository string           `json:"repository"`
	Version    string           `json:"version"`
	Commit     string           `json:"commit"`
	Channel    string           `json:"channel"`
	Sequence   uint64           `json:"sequence"`
	KeyID      string           `json:"key_id"`
	Packages   []IndexedPackage `json:"packages"`
}

type IndexedPackage struct {
	ID              string `json:"id"`
	Manifest        string `json:"manifest_asset"`
	Signature       string `json:"signature_asset"`
	Archive         string `json:"package_asset"`
	ManifestSHA256  string `json:"manifest_sha256"`
	SignatureSHA256 string `json:"signature_sha256"`
	PackageSHA256   string `json:"package_sha256"`
	PackageBytes    int64  `json:"package_bytes"`
}

var packageID = regexp.MustCompile(`^[a-z0-9][a-z0-9-]{0,47}$`)

func SignIndex(raw []byte, keyPath string, trust Trust, dir, signaturePath string) error {
	sig, err := signBytes(raw, keyPath)
	if err != nil {
		return err
	}
	if err := VerifyIndexDirectory(raw, sig, trust, dir); err != nil {
		return err
	}
	return durableFile(signaturePath, sig, 0644)
}

func (x ReleaseIndex) Validate() error {
	if x.Schema != 1 || x.Policy != Policy || x.Type != "application-index" || !repoPattern.MatchString(x.Repository) || !commitPattern.MatchString(x.Commit) || x.Sequence == 0 || !token.MatchString(x.KeyID) {
		return fmt.Errorf("invalid release index identity")
	}
	if _, err := parseVersion(x.Version); err != nil {
		return err
	}
	if x.Channel != "stable" && x.Channel != "testing" {
		return fmt.Errorf("invalid index channel")
	}
	if len(x.Packages) == 0 || len(x.Packages) > 32 {
		return fmt.Errorf("index must contain 1..32 packages")
	}
	seen := map[string]bool{}
	for _, p := range x.Packages {
		if !packageID.MatchString(p.ID) || seen[p.ID] {
			return fmt.Errorf("invalid/duplicate package ID")
		}
		seen[p.ID] = true
		if p.Manifest != p.ID+"--manifest.json" || p.Signature != p.ID+"--manifest.sig" || p.Archive != p.ID+"--application.tar.gz" {
			return fmt.Errorf("invalid indexed asset names")
		}
		if !sha256Pattern.MatchString(p.ManifestSHA256) || !sha256Pattern.MatchString(p.SignatureSHA256) || !sha256Pattern.MatchString(p.PackageSHA256) || p.PackageBytes <= 0 || p.PackageBytes > 256<<20 {
			return fmt.Errorf("invalid indexed hash/size")
		}
	}
	return nil
}

func VerifyIndex(raw, sig []byte, trust Trust) (ReleaseIndex, error) {
	var x ReleaseIndex
	if err := StrictJSON(raw, &x); err != nil {
		return x, err
	}
	if err := x.Validate(); err != nil {
		return x, err
	}
	return x, verifySignature(raw, sig, trust, x.KeyID, x.Sequence)
}

func digestBytes(b []byte) string { h := sha256.Sum256(b); return hex.EncodeToString(h[:]) }

// SelectIndexDirectory shares signature and target rules with online selection.
// Engine.Install still rechecks live identity/trust and transaction policy.
func SelectIndexDirectory(dir, repository, channel string, d Device, trust Trust) (*Candidate, error) {
	raw, err := os.ReadFile(filepath.Join(dir, "release-index.json"))
	if err != nil {
		return nil, err
	}
	sig, err := os.ReadFile(filepath.Join(dir, "release-index.sig"))
	if err != nil {
		return nil, err
	}
	if err := VerifyIndexDirectory(raw, sig, trust, dir); err != nil {
		return nil, err
	}
	x, err := VerifyIndex(raw, sig, trust)
	if err != nil {
		return nil, err
	}
	if x.Repository != repository || x.Channel != channel {
		return nil, fmt.Errorf("offline index repository/channel mismatch")
	}
	var selected *Candidate
	for _, p := range x.Packages {
		b, err := os.ReadFile(filepath.Join(dir, p.Manifest))
		if err != nil {
			return nil, err
		}
		s, err := os.ReadFile(filepath.Join(dir, p.Signature))
		if err != nil {
			return nil, err
		}
		m, err := x.verifyPackage(p, b, s, trust)
		if err != nil {
			return nil, err
		}
		if Match(m, d) != nil {
			continue
		}
		if selected != nil {
			return nil, fmt.Errorf("ambiguous offline target")
		}
		selected = &Candidate{m, b, s, Asset{Name: p.Archive, Size: p.PackageBytes}}
	}
	if selected == nil {
		return nil, fmt.Errorf("no compatible offline target")
	}
	return selected, nil
}

func (x ReleaseIndex) verifyPackage(p IndexedPackage, raw, sig []byte, trust Trust) (Manifest, error) {
	var m Manifest
	if digestBytes(raw) != p.ManifestSHA256 || digestBytes(sig) != p.SignatureSHA256 {
		return m, fmt.Errorf("indexed manifest/signature hash mismatch")
	}
	m, err := Verify(raw, sig, trust)
	if err != nil {
		return m, err
	}
	if m.Version != x.Version || m.Commit != x.Commit || m.Channel != x.Channel || m.Sequence != x.Sequence || m.PackageSHA256 != p.PackageSHA256 || m.PackageBytes != p.PackageBytes {
		return m, fmt.Errorf("index/package metadata mismatch")
	}
	return m, nil
}

// VerifyIndexDirectory is also used by offline publication tools; every package
// is checked, including packages that would not match the publisher's machine.
func VerifyIndexDirectory(raw, sig []byte, trust Trust, dir string) error {
	x, err := VerifyIndex(raw, sig, trust)
	if err != nil {
		return err
	}
	identities := map[string]bool{}
	for _, p := range x.Packages {
		for _, name := range []string{p.Manifest, p.Signature, p.Archive} {
			st, err := os.Lstat(filepath.Join(dir, name))
			if err != nil {
				return err
			}
			if !st.Mode().IsRegular() {
				return fmt.Errorf("indexed asset is not regular")
			}
		}
		b, err := os.ReadFile(filepath.Join(dir, p.Manifest))
		if err != nil {
			return err
		}
		s, err := os.ReadFile(filepath.Join(dir, p.Signature))
		if err != nil {
			return err
		}
		m, err := x.verifyPackage(p, b, s, trust)
		if err != nil {
			return err
		}
		for _, t := range m.Targets {
			// Exact identity ownership is unique; overlapping version ranges must
			// be represented by one package, not ambiguous competing artifacts.
			key := ""
			for _, k := range identityKeys {
				key += k + "=" + t.Identity[k] + "\n"
			}
			if identities[key] {
				return fmt.Errorf("ambiguous target across indexed packages")
			}
			identities[key] = true
		}
		if err := CheckArchive(filepath.Join(dir, p.Archive), m, ""); err != nil {
			return err
		}
	}
	return nil
}

func (g *GitHub) indexedCandidate(ctx context.Context, r Release, assets map[string]Asset, d Device, trust Trust, channel, current string, floor uint64) (*Candidate, error) {
	ma, mok := assets["release-index.json"]
	sa, sok := assets["release-index.sig"]
	if !mok || !sok {
		return nil, fmt.Errorf("incomplete signed release index")
	}
	raw, err := g.bytes(ctx, ma.URL, 1<<20)
	if err != nil {
		return nil, err
	}
	sig, err := g.bytes(ctx, sa.URL, 4096)
	if err != nil {
		return nil, err
	}
	x, err := VerifyIndex(raw, sig, trust)
	if err != nil {
		return nil, err
	}
	if x.Repository != g.Repository || r.Tag != "v"+x.Version || r.Prerelease != (x.Channel == "testing") {
		return nil, fmt.Errorf("index/repository/tag/channel mismatch")
	}
	if x.Channel != channel || x.Sequence <= floor {
		return nil, nil
	}
	newer, err := Compare(x.Version, current)
	if err != nil {
		return nil, err
	}
	if newer <= 0 {
		return nil, nil
	}
	var selected *Candidate
	for _, p := range x.Packages {
		masset, mok := assets[p.Manifest]
		sasset, sok := assets[p.Signature]
		a, aok := assets[p.Archive]
		if !mok || !sok || !aok || a.Size != p.PackageBytes {
			return nil, fmt.Errorf("missing/mismatched indexed asset")
		}
		b, err := g.bytes(ctx, masset.URL, 1<<20)
		if err != nil {
			return nil, err
		}
		s, err := g.bytes(ctx, sasset.URL, 4096)
		if err != nil {
			return nil, err
		}
		m, err := x.verifyPackage(p, b, s, trust)
		if err != nil {
			return nil, err
		}
		if Match(m, d) != nil {
			continue
		}
		if selected != nil {
			return nil, fmt.Errorf("multiple indexed packages match device")
		}
		selected = &Candidate{m, b, s, a}
	}
	return selected, nil
}
