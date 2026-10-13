package update

import (
	"crypto"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/pem"
	"fmt"
)

type SystemManifest struct {
	BuildProvenanceSHA256    string     `json:"build_provenance_sha256,omitempty"`
	Schema                   int        `json:"schema_version"`
	Policy                   string     `json:"policy"`
	Type                     string     `json:"release_type"`
	Version                  string     `json:"version"`
	Commit                   string     `json:"commit"`
	KeyID                    string     `json:"key_id"`
	Sequence                 uint64     `json:"sequence"`
	ImageSHA256              string     `json:"image_sha256"`
	ImageBytes               int64      `json:"image_bytes"`
	Targets                  []Target   `json:"targets"`
	Bootloader               Range      `json:"bootloader"`
	PartitionSHA256          string     `json:"partition_sha256"`
	Storage                  string     `json:"storage"`
	MinStorageBytes          int64      `json:"min_storage_bytes"`
	BootMode                 string     `json:"boot_mode"`
	FactoryManifestSHA256    string     `json:"factory_manifest_sha256"`
	FactoryApplicationSHA256 string     `json:"factory_application_sha256"`
	SDKComponentsSHA256      string     `json:"sdk_components_sha256"`
	OTA                      *SystemOTA `json:"ota,omitempty"`
}

// OTA describes the separately hashed SWUpdate artifact and the resulting
// runtime. Targets describe the SOURCE systems allowed to install it.
type SystemOTA struct {
	Channel       string `json:"channel"`
	PackageSHA256 string `json:"package_sha256"`
	PackageBytes  int64  `json:"package_bytes"`
	Device        Device `json:"destination_device"`
}
type SystemDevice struct {
	Device          Device `json:"device"`
	Bootloader      string `json:"bootloader"`
	PartitionSHA256 string `json:"partition_sha256"`
	Storage         string `json:"storage"`
	StorageBytes    int64  `json:"storage_bytes"`
	BootMode        string `json:"boot_mode"`
}

func verifyDetached(raw, sig []byte, key string, sequence uint64, t Trust) error {
	if t.Schema != 1 || sequence < t.MinSequence {
		return fmt.Errorf("untrusted signing policy/sequence")
	}
	for _, revoked := range t.Revoked {
		if revoked == key {
			return fmt.Errorf("revoked key")
		}
	}
	block, _ := pem.Decode([]byte(t.Keys[key]))
	if block == nil {
		return fmt.Errorf("unknown signing key")
	}
	k, e := x509.ParsePKIXPublicKey(block.Bytes)
	if e != nil {
		return e
	}
	pub, ok := k.(*rsa.PublicKey)
	if !ok || pub.N.BitLen() < 3072 {
		return fmt.Errorf("invalid RSA public key")
	}
	h := sha256.Sum256(raw)
	return rsa.VerifyPKCS1v15(pub, crypto.SHA256, h[:], sig)
}

// VerifySystem is a release/installer admission check; it never flashes a device.
func VerifySystemManifest(raw, sig []byte, t Trust, d SystemDevice) (SystemManifest, error) {
	var m SystemManifest
	if e := StrictJSON(raw, &m); e != nil {
		return m, e
	}
	if m.Schema != 1 || m.Policy != Policy || m.Type != "system" || !commitPattern.MatchString(m.Commit) || m.Sequence == 0 {
		return m, fmt.Errorf("invalid system manifest")
	}
	if _, e := parseVersion(m.Version); e != nil {
		return m, e
	}
	for _, hash := range []string{m.ImageSHA256, m.PartitionSHA256, m.FactoryManifestSHA256, m.FactoryApplicationSHA256, m.SDKComponentsSHA256} {
		if !sha256Pattern.MatchString(hash) {
			return m, fmt.Errorf("missing system provenance hash")
		}
	}
	if e := verifyDetached(raw, sig, m.KeyID, m.Sequence, t); e != nil {
		return m, e
	}
	if e := validateTargets(m.Targets); e != nil {
		return m, e
	}
	if e := d.Device.Validate(); e != nil {
		return m, e
	}
	if e := matchTargets(m.Targets, d.Device); e != nil {
		return m, e
	}
	if e := m.Bootloader.Validate(); e != nil {
		return m, e
	}
	if !m.Bootloader.Contains(d.Bootloader) || !token.MatchString(m.Storage) || !token.MatchString(m.BootMode) || m.Storage != d.Storage || m.BootMode != d.BootMode || m.MinStorageBytes <= 0 || d.StorageBytes < m.MinStorageBytes || m.PartitionSHA256 != d.PartitionSHA256 {
		return m, fmt.Errorf("boot chain/storage/partition mismatch")
	}
	if m.BuildProvenanceSHA256 != "" && !sha256Pattern.MatchString(m.BuildProvenanceSHA256) {
		return m, fmt.Errorf("invalid build provenance hash")
	}
	if m.ImageBytes <= 0 {
		return m, fmt.Errorf("invalid system image size")
	}
	if m.OTA != nil {
		o := m.OTA
		v, _ := parseVersion(m.Version)
		if o.Channel == "stable" && len(v.pre) != 0 {
			return m, fmt.Errorf("prerelease system cannot use stable channel")
		}
		if (o.Channel != "stable" && o.Channel != "testing") || !sha256Pattern.MatchString(o.PackageSHA256) || o.PackageBytes <= 0 || o.PackageBytes > 4<<30 || o.Device.Enrollment != "" {
			return m, fmt.Errorf("invalid system OTA metadata")
		}
		next := o.Device
		next.Enrollment = d.Device.Enrollment
		if err := next.Validate(); err != nil {
			return m, err
		}
		for _, k := range identityKeys[:10] {
			if next.Identity[k] != d.Device.Identity[k] {
				return m, fmt.Errorf("system OTA changes hardware identity: %s", k)
			}
		}
		if next.Runtime["system"] != m.Version || next.DataSchema != d.Device.DataSchema || next.SystemBaseline == d.Device.SystemBaseline {
			return m, fmt.Errorf("system destination version/baseline/data mismatch")
		}
	}
	return m, nil
}

func VerifySystem(raw, sig []byte, t Trust, d SystemDevice, image string) (SystemManifest, error) {
	m, e := VerifySystemManifest(raw, sig, t, d)
	if e != nil {
		return m, e
	}
	hash, size, e := hashFile(image)
	if e != nil {
		return m, e
	}
	if hash != m.ImageSHA256 || size != m.ImageBytes || size <= 0 {
		return m, fmt.Errorf("system image hash/size mismatch")
	}
	return m, nil
}
