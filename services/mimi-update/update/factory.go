package update

import (
	"os"
	"path/filepath"
)

// ExtractFactory prepares a new build directory. It does not enroll a device or
// change the running system; it applies the same signature/target/archive gates.
func ExtractFactory(archive string, raw, sig []byte, trust Trust, device Device, out string) error {
	m, err := Verify(raw, sig, trust)
	if err != nil {
		return err
	}
	if err = Match(m, device); err != nil {
		return err
	}
	if err = os.Mkdir(out, 0755); err != nil {
		return err
	}
	complete := false
	defer func() {
		if !complete {
			os.RemoveAll(out)
		}
	}()
	if err = CheckArchive(archive, m, out); err != nil {
		return err
	}
	if err = durableFile(filepath.Join(out, "manifest.json"), raw, 0644); err != nil {
		return err
	}
	if err = durableFile(filepath.Join(out, "manifest.sig"), sig, 0644); err != nil {
		return err
	}
	if err = VerifyTree(out, m); err != nil {
		return err
	}
	if err = syncDir(out); err != nil {
		return err
	}
	complete = true
	return nil
}
