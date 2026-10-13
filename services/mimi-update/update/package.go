package update

import (
	"archive/tar"
	"compress/gzip"
	"crypto"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"strings"
)

type Trust struct {
	Schema      int               `json:"schema_version"`
	Keys        map[string]string `json:"keys"`
	Revoked     []string          `json:"revoked"`
	MinSequence uint64            `json:"min_sequence"`
}

func Verify(raw, sig []byte, trust Trust) (Manifest, error) {
	var m Manifest
	if e := StrictJSON(raw, &m); e != nil {
		return m, e
	}
	if e := m.Validate(); e != nil {
		return m, e
	}
	return m, verifySignature(raw, sig, trust, m.KeyID, m.Sequence)
}

func verifySignature(raw, sig []byte, trust Trust, keyID string, sequence uint64) error {
	if trust.Schema != 1 || sequence < trust.MinSequence {
		return fmt.Errorf("untrusted policy or revoked sequence")
	}
	for _, id := range trust.Revoked {
		if id == keyID {
			return fmt.Errorf("revoked signing key")
		}
	}
	block, _ := pem.Decode([]byte(trust.Keys[keyID]))
	if block == nil {
		return fmt.Errorf("unknown signing key")
	}
	key, e := x509.ParsePKIXPublicKey(block.Bytes)
	if e != nil {
		return e
	}
	pub, ok := key.(*rsa.PublicKey)
	if !ok || pub.N.BitLen() < 3072 {
		return fmt.Errorf("RSA key must be at least 3072 bits")
	}
	digest := sha256.Sum256(raw)
	if e = rsa.VerifyPKCS1v15(pub, crypto.SHA256, digest[:], sig); e != nil {
		return fmt.Errorf("manifest signature rejected: %w", e)
	}
	return nil
}
func allowedPath(s string) bool {
	if s == "" || strings.ContainsAny(s, "\\\x00") || strings.HasPrefix(s, "/") || path.Clean(s) != s {
		return false
	}
	if s == "bin/lvglsim" {
		return true
	}
	for _, prefix := range []string{"fonts/", "resources/", "www/", "lib/"} {
		if strings.HasPrefix(s, prefix) && len(s) > len(prefix) {
			return true
		}
	}
	return false
}
func hashFile(p string) (string, int64, error) {
	f, e := os.Open(p)
	if e != nil {
		return "", 0, e
	}
	defer f.Close()
	h := sha256.New()
	n, e := io.Copy(h, f)
	return hex.EncodeToString(h.Sum(nil)), n, e
}
func syncDir(p string) error {
	f, e := os.Open(p)
	if e != nil {
		return e
	}
	defer f.Close()
	return f.Sync()
}
func durableFile(p string, b []byte, mode os.FileMode) error {
	f, e := os.OpenFile(p, os.O_CREATE|os.O_EXCL|os.O_WRONLY, mode)
	if e != nil {
		return e
	}
	if _, e = f.Write(b); e == nil {
		e = f.Sync()
	}
	ce := f.Close()
	if e != nil {
		return e
	}
	return ce
}
func CheckArchive(file string, m Manifest, dest string) error {
	hash, n, e := hashFile(file)
	if e != nil {
		return e
	}
	if n != m.PackageBytes || hash != m.PackageSHA256 {
		return fmt.Errorf("package hash/size mismatch")
	}
	f, e := os.Open(file)
	if e != nil {
		return e
	}
	defer f.Close()
	compressed := &io.LimitedReader{R: f, N: m.PackageBytes + 1}
	compressedHash := sha256.New()
	gz, e := gzip.NewReader(io.TeeReader(compressed, compressedHash))
	if e != nil {
		return e
	}
	defer gz.Close()
	// Bound all inflated bytes, including headers, directory padding and trailers.
	limit := m.ExpandedBytes + int64(m.MaxFiles)*2048 + 1<<20
	bounded := &io.LimitedReader{R: gz, N: limit + 1}
	tr := tar.NewReader(bounded)
	seen := map[string]bool{}
	for {
		h, e := tr.Next()
		if e == io.EOF {
			break
		}
		if e != nil {
			return e
		}
		if h.Typeflag != tar.TypeReg || !allowedPath(h.Name) || seen[h.Name] || len(h.PAXRecords) > 0 {
			return fmt.Errorf("unsafe/duplicate tar member %q", h.Name)
		}
		want, ok := m.Files[h.Name]
		if !ok || want.Size != h.Size || int64(want.Mode) != h.Mode {
			return fmt.Errorf("file not in signed inventory: %s", h.Name)
		}
		seen[h.Name] = true
		digest := sha256.New()
		var out *os.File
		var writer io.Writer = digest
		if dest != "" {
			p := filepath.Join(dest, filepath.FromSlash(h.Name))
			if e = os.MkdirAll(filepath.Dir(p), 0755); e != nil {
				return e
			}
			out, e = os.OpenFile(p, os.O_WRONLY|os.O_CREATE|os.O_EXCL, os.FileMode(want.Mode))
			if e != nil {
				return e
			}
			writer = io.MultiWriter(digest, out)
		}
		_, e = io.Copy(writer, tr)
		if out != nil {
			if e == nil {
				e = out.Sync()
			}
			ce := out.Close()
			if e == nil {
				e = ce
			}
		}
		if e != nil {
			return e
		}
		if hex.EncodeToString(digest.Sum(nil)) != want.SHA256 {
			return fmt.Errorf("file hash mismatch: %s", h.Name)
		}
	}
	if _, e = io.Copy(io.Discard, bounded); e != nil {
		return e
	}
	if bounded.N <= 0 {
		return fmt.Errorf("archive inflated size exceeded")
	}
	if compressed.N != 1 || hex.EncodeToString(compressedHash.Sum(nil)) != m.PackageSHA256 {
		return fmt.Errorf("archive changed while reading")
	}
	if len(seen) != len(m.Files) {
		return fmt.Errorf("missing files")
	}
	if dest != "" {
		return filepath.WalkDir(dest, func(p string, d os.DirEntry, e error) error {
			if e != nil {
				return e
			}
			if d.IsDir() {
				return syncDir(p)
			}
			return nil
		})
	}
	return nil
}
func VerifyTree(dir string, m Manifest) error {
	seen := 0
	e := filepath.WalkDir(dir, func(p string, d os.DirEntry, e error) error {
		if e != nil {
			return e
		}
		if p == dir {
			return nil
		}
		if d.Type()&os.ModeSymlink != 0 {
			return fmt.Errorf("symlink in release")
		}
		if d.IsDir() {
			return nil
		}
		rel, e := filepath.Rel(dir, p)
		if e != nil {
			return e
		}
		if rel == "manifest.json" || rel == "manifest.sig" {
			return nil
		}
		want, ok := m.Files[filepath.ToSlash(rel)]
		if !ok {
			return fmt.Errorf("unexpected installed file %s", rel)
		}
		info, e := d.Info()
		if e != nil {
			return e
		}
		if !info.Mode().IsRegular() || info.Mode().Perm() != os.FileMode(want.Mode) {
			return fmt.Errorf("installed file mode mismatch")
		}
		hash, n, e := hashFile(p)
		if e != nil {
			return e
		}
		if hash != want.SHA256 || n != want.Size {
			return fmt.Errorf("installed file corrupted: %s", rel)
		}
		seen++
		return nil
	})
	if e != nil {
		return e
	}
	if seen != len(m.Files) {
		return fmt.Errorf("installed inventory incomplete")
	}
	return nil
}
