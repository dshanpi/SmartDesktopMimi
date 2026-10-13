package update

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"syscall"
)

// Persist the signed bytes, never a trusted interpretation of an unsigned cache.
type SystemCandidate struct {
	Raw              []byte `json:"manifest"`
	Signature        []byte `json:"signature"`
	FactoryRaw       []byte `json:"factory_manifest"`
	FactorySignature []byte `json:"factory_signature"`
	Package          Asset  `json:"package"`
}

func (c SystemCandidate) Verify(d SystemDevice, trust Trust, channel string, floor uint64) (SystemManifest, error) {
	m, err := VerifySystemManifest(c.Raw, c.Signature, trust, d)
	if err != nil {
		return m, err
	}
	if m.OTA == nil || m.OTA.Channel != channel || m.Sequence <= floor || c.Package.Size != m.OTA.PackageBytes || !safeGitHubURL(c.Package.URL) {
		return m, fmt.Errorf("system OTA channel/sequence/asset mismatch")
	}
	newer, err := Compare(m.Version, d.Device.Runtime["system"])
	if err != nil || newer <= 0 {
		return m, fmt.Errorf("system downgrade or same version rejected")
	}
	hash := sha256.Sum256(c.FactoryRaw)
	if hex.EncodeToString(hash[:]) != m.FactoryManifestSHA256 {
		return m, fmt.Errorf("factory manifest hash mismatch")
	}
	factory, err := Verify(c.FactoryRaw, c.FactorySignature, trust)
	if err != nil {
		return m, err
	}
	next := m.OTA.Device
	next.Enrollment = d.Device.Enrollment
	if factory.PackageSHA256 != m.FactoryApplicationSHA256 {
		return m, fmt.Errorf("factory package mismatch")
	}
	if err = Match(factory, next); err != nil {
		return m, fmt.Errorf("destination factory incompatible: %w", err)
	}
	return m, nil
}

func (g *GitHub) CheckSystem(ctx context.Context, d SystemDevice, trust Trust, channel string, floor uint64) (*SystemCandidate, error) {
	if err := d.Device.Validate(); err != nil {
		return nil, err
	}
	if channel != "stable" && channel != "testing" {
		return nil, fmt.Errorf("unknown system channel")
	}
	var best *SystemCandidate
	var bestManifest SystemManifest
	for page := 1; page <= 10; page++ {
		raw, err := g.bytes(ctx, fmt.Sprintf("https://api.github.com/repos/%s/releases?per_page=100&page=%d", g.Repository, page), 4<<20)
		if err != nil {
			return nil, err
		}
		var releases []Release
		if err = json.Unmarshal(raw, &releases); err != nil {
			return nil, err
		}
		for _, r := range releases {
			kind, platform, _ := releaseIdentity(r.Tag)
			if kind != "system" || platform != d.Device.Identity["platform_id"] {
				continue
			}
			if r.Draft || r.Prerelease != (channel == "testing") {
				continue
			}
			assets := map[string]Asset{}
			duplicate := false
			for _, a := range r.Assets {
				if _, ok := assets[a.Name]; ok {
					duplicate = true
				}
				assets[a.Name] = a
			}
			names := []string{"system-manifest.json", "system-manifest.sig", "factory-manifest.json", "factory-manifest.sig", "system.swu"}
			for _, name := range names {
				if _, ok := assets[name]; !ok {
					duplicate = true
				}
			}
			if duplicate {
				continue
			}
			var c SystemCandidate
			fields := make([][]byte, 4)
			for i := range fields {
				limit := int64(1 << 20)
				if i%2 == 1 {
					limit = 4096
				}
				fields[i], err = g.bytes(ctx, assets[names[i]].URL, limit)
				if err != nil {
					return nil, err
				}
			}
			c.Raw, c.Signature, c.FactoryRaw, c.FactorySignature = fields[0], fields[1], fields[2], fields[3]
			c.Package = assets["system.swu"]
			m, err := c.Verify(d, trust, channel, floor)
			tag, tagErr := systemReleaseTag(m)
			if err != nil || tagErr != nil || r.Tag != tag {
				continue
			}
			if best == nil {
				best, bestManifest = &c, m
				continue
			}
			n, _ := Compare(m.Version, bestManifest.Version)
			if n == 0 && (m.OTA.PackageSHA256 != bestManifest.OTA.PackageSHA256 || m.Sequence != bestManifest.Sequence) {
				return nil, fmt.Errorf("conflicting system release")
			}
			if n > 0 {
				best, bestManifest = &c, m
			}
		}
		if len(releases) < 100 {
			return best, nil
		}
	}
	return nil, fmt.Errorf("system release pagination limit exceeded")
}

func (g *GitHub) DownloadSystem(ctx context.Context, c SystemCandidate, d SystemDevice, trust Trust, channel string, floor uint64, dir string) (string, error) {
	m, err := c.Verify(d, trust, channel, floor)
	if err != nil {
		return "", err
	}
	if err = os.MkdirAll(dir, 0700); err != nil {
		return "", err
	}
	var fs syscall.Statfs_t
	if err = syscall.Statfs(dir, &fs); err != nil {
		return "", err
	}
	if uint64(fs.Bavail)*uint64(fs.Bsize) < uint64(m.OTA.PackageBytes)+(64<<20) {
		return "", fmt.Errorf("insufficient system download space")
	}
	f, err := os.CreateTemp(dir, ".system-download-")
	if err != nil {
		return "", err
	}
	tmp := f.Name()
	defer os.Remove(tmp)
	err = g.get(ctx, c.Package.URL, m.OTA.PackageBytes, &downloadProgress{writer: f, total: m.OTA.PackageBytes, report: g.Progress})
	if err == nil {
		err = f.Sync()
	}
	closed := f.Close()
	if err != nil {
		return "", err
	}
	if closed != nil {
		return "", closed
	}
	hash, size, err := hashFile(tmp)
	if err != nil {
		return "", err
	}
	if hash != m.OTA.PackageSHA256 || size != m.OTA.PackageBytes {
		return "", fmt.Errorf("system OTA hash/size mismatch")
	}
	dest := filepath.Join(dir, m.OTA.PackageSHA256+".swu")
	if err = os.Rename(tmp, dest); err != nil {
		return "", err
	}
	return dest, syncDir(dir)
}

// VerifySystemOTA is the same admission gate used by release CI and installers.
func VerifySystemOTA(raw, signature, factory, factorySignature []byte, trust Trust, d SystemDevice, archive string) error {
	m, err := VerifySystemManifest(raw, signature, trust, d)
	if err != nil {
		return err
	}
	if m.OTA == nil {
		return fmt.Errorf("system release has no OTA artifact")
	}
	c := SystemCandidate{raw, signature, factory, factorySignature, Asset{"system.swu", "https://github.com/", m.OTA.PackageBytes}}
	if _, err = c.Verify(d, trust, m.OTA.Channel, 0); err != nil {
		return err
	}
	hash, size, err := hashFile(archive)
	if err != nil {
		return err
	}
	if hash != m.OTA.PackageSHA256 || size != m.OTA.PackageBytes {
		return fmt.Errorf("OTA artifact hash/size mismatch")
	}
	return nil
}
