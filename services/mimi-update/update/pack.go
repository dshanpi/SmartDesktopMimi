package update

import (
	"archive/tar"
	"compress/gzip"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"debug/elf"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Pack accepts an already curated application tree, never a rootfs. Each ELF's
// loader and linked SONAMEs must be explicitly declared by every target.
func Pack(src, out, keyPath string, m Manifest, epoch int64) error {
	if e := os.Mkdir(out, 0700); e != nil {
		return e
	}
	complete := false
	defer func() {
		if !complete {
			os.RemoveAll(out)
		}
	}()
	m.Files = map[string]File{}
	m.ExpandedBytes = 0
	var names []string
	e := filepath.WalkDir(src, func(p string, d os.DirEntry, e error) error {
		if e != nil {
			return e
		}
		if p == src {
			return nil
		}
		if d.Type()&os.ModeSymlink != 0 {
			return fmt.Errorf("symlink in source")
		}
		if d.IsDir() {
			return nil
		}
		rel, e := filepath.Rel(src, p)
		if e != nil {
			return e
		}
		rel = filepath.ToSlash(rel)
		if !allowedPath(rel) {
			return fmt.Errorf("not an application payload: %s", rel)
		}
		info, e := d.Info()
		if e != nil {
			return e
		}
		if !info.Mode().IsRegular() {
			return fmt.Errorf("nonregular payload")
		}
		if rel == "bin/lvglsim" || strings.HasPrefix(rel, "lib/") {
			if e = checkELF(p, m.Targets); e != nil {
				return e
			}
		}
		hash, n, e := hashFile(p)
		if e != nil {
			return e
		}
		mode := uint32(0644)
		if rel == "bin/lvglsim" {
			mode = 0755
		}
		m.Files[rel] = File{hash, n, mode}
		m.ExpandedBytes += n
		names = append(names, rel)
		return nil
	})
	if e != nil {
		return e
	}
	sort.Strings(names)
	m.MaxFiles = len(names)
	f, e := os.OpenFile(filepath.Join(out, "application.tar.gz"), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
	if e != nil {
		return e
	}
	gz := gzip.NewWriter(f)
	tw := tar.NewWriter(gz)
	for _, name := range names {
		entry := m.Files[name]
		if e = tw.WriteHeader(&tar.Header{Name: name, Mode: int64(entry.Mode), Size: entry.Size, ModTime: time.Unix(epoch, 0), Format: tar.FormatUSTAR}); e != nil {
			break
		}
		var in *os.File
		in, e = os.Open(filepath.Join(src, name))
		if e != nil {
			break
		}
		_, e = io.Copy(tw, in)
		in.Close()
		if e != nil {
			break
		}
	}
	for _, closeFn := range []func() error{tw.Close, gz.Close, f.Sync, f.Close} {
		if ce := closeFn(); e == nil {
			e = ce
		}
	}
	if e != nil {
		return e
	}
	m.PackageSHA256, m.PackageBytes, e = hashFile(filepath.Join(out, "application.tar.gz"))
	if e != nil {
		return e
	}
	if e = m.Validate(); e != nil {
		return e
	}
	if e = CheckArchive(filepath.Join(out, "application.tar.gz"), m, ""); e != nil {
		return e
	}
	raw, e := json.MarshalIndent(m, "", "  ")
	if e != nil {
		return e
	}
	sig, e := signBytes(raw, keyPath)
	if e != nil {
		return e
	}
	if e = durableFile(filepath.Join(out, "manifest.json"), raw, 0644); e != nil {
		return e
	}
	if e = durableFile(filepath.Join(out, "manifest.sig"), sig, 0644); e != nil {
		return e
	}
	if e = syncDir(out); e != nil {
		return e
	}
	complete = true
	return nil
}
func checkELF(p string, targets []Target) error {
	f, e := elf.Open(p)
	if e != nil {
		return fmt.Errorf("ELF %s: %w", p, e)
	}
	defer f.Close()
	if f.Type != elf.ET_EXEC && f.Type != elf.ET_DYN {
		return fmt.Errorf("not executable/shared ELF")
	}
	machines := map[string]elf.Machine{"aarch64": elf.EM_AARCH64, "arm": elf.EM_ARM, "x86_64": elf.EM_X86_64}
	interp := ""
	for _, pr := range f.Progs {
		if pr.Type == elf.PT_INTERP {
			b, e := io.ReadAll(io.LimitReader(pr.Open(), 4096))
			if e != nil {
				return e
			}
			interp = strings.TrimRight(string(b), "\x00")
		}
	}
	libs, e := f.ImportedLibraries()
	if e != nil {
		return e
	}
	for _, t := range targets {
		machine, ok := machines[t.Identity["architecture"]]
		if !ok || machine != f.Machine {
			return fmt.Errorf("ELF architecture mismatch")
		}
		bits := "32"
		if f.Class == elf.ELFCLASS64 {
			bits = "64"
		}
		endian := "little"
		if f.Data == elf.ELFDATA2MSB {
			endian = "big"
		}
		if bits != t.Identity["bits"] || endian != t.Identity["endian"] {
			return fmt.Errorf("ELF bits/endian mismatch")
		}
		if interp != "" && interp != t.Identity["loader"] {
			return fmt.Errorf("ELF loader mismatch: %s", interp)
		}
		for _, lib := range libs {
			if _, ok := t.Runtime["soname:"+lib]; !ok {
				return fmt.Errorf("undeclared ELF dependency %s", lib)
			}
		}
	}
	return nil
}

func signBytes(raw []byte, keyPath string) ([]byte, error) {
	kb, e := os.ReadFile(keyPath)
	if e != nil {
		return nil, e
	}
	block, _ := pem.Decode(kb)
	if block == nil {
		return nil, fmt.Errorf("invalid signing key")
	}
	var key *rsa.PrivateKey
	if block.Type == "RSA PRIVATE KEY" {
		key, e = x509.ParsePKCS1PrivateKey(block.Bytes)
	} else {
		var k any
		k, e = x509.ParsePKCS8PrivateKey(block.Bytes)
		if e == nil {
			var ok bool
			key, ok = k.(*rsa.PrivateKey)
			if !ok {
				return nil, fmt.Errorf("signing key must be RSA")
			}
		}
	}
	if e != nil {
		return nil, e
	}
	if key.N.BitLen() < 3072 {
		return nil, fmt.Errorf("RSA key too short")
	}
	digest := sha256.Sum256(raw)
	sig, e := rsa.SignPKCS1v15(rand.Reader, key, crypto.SHA256, digest[:])
	if e != nil {
		return nil, e
	}
	return sig, nil
}
