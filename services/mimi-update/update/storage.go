package update

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

// StorageReport is an observation, not a filesystem repair or a hardware test.
type StorageReport struct {
	Passed  bool            `json:"passed"`
	Volumes []StorageVolume `json:"volumes"`
}
type StorageVolume struct {
	Path       string  `json:"path"`
	Mount      string  `json:"mount,omitempty"`
	Filesystem string  `json:"filesystem,omitempty"`
	Device     string  `json:"device,omitempty"`
	Errors     *uint64 `json:"ext4_errors_since_fsck,omitempty"`
	Problem    string  `json:"problem,omitempty"`
}
type storageMount struct{ path, device, fs, options, superOptions string }

var blockDeviceName = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9_.-]*$`)

func mountPath(s string) string {
	return strings.NewReplacer(`\040`, " ", `\011`, "\t", `\012`, "\n", `\134`, `\`).Replace(s)
}
func storageMounts(root string) ([]storageMount, error) {
	f, err := os.Open(filepath.Join(root, "proc/self/mountinfo"))
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var mounts []storageMount
	s := bufio.NewScanner(f)
	s.Buffer(make([]byte, 4096), 1<<20)
	for s.Scan() {
		fields := strings.Fields(s.Text())
		sep := -1
		for i, field := range fields {
			if field == "-" {
				sep = i
				break
			}
		}
		if sep < 6 || len(fields) != sep+4 {
			return nil, fmt.Errorf("malformed mountinfo")
		}
		p := mountPath(fields[4])
		if !filepath.IsAbs(p) || filepath.Clean(p) != p {
			return nil, fmt.Errorf("invalid mount path")
		}
		mounts = append(mounts, storageMount{p, fields[2], fields[sep+1], fields[5], fields[sep+3]})
	}
	if err = s.Err(); err != nil {
		return nil, err
	}
	if len(mounts) == 0 {
		return nil, fmt.Errorf("mountinfo is empty")
	}
	return mounts, nil
}

// Resolve even a not-yet-created update directory through its existing parent.
func existingStoragePath(root, path string) (string, error) {
	if !filepath.IsAbs(path) || filepath.Clean(path) != path {
		return "", fmt.Errorf("invalid storage path")
	}
	p := filepath.Join(root, path)
	for {
		_, err := os.Lstat(p)
		if err == nil {
			real, err := filepath.EvalSymlinks(p)
			if err != nil {
				return "", err
			}
			info, err := os.Stat(real)
			if err != nil {
				return "", err
			}
			if !info.IsDir() {
				return "", fmt.Errorf("storage path is not a directory")
			}
			rel, err := filepath.Rel(root, real)
			if err != nil || rel == ".." || strings.HasPrefix(rel, "../") {
				return "", fmt.Errorf("storage path escaped root")
			}
			return filepath.Clean("/" + rel), nil
		}
		if !os.IsNotExist(err) {
			return "", err
		}
		if p == root || p == "/" {
			return "", fmt.Errorf("storage parent missing")
		}
		p = filepath.Dir(p)
	}
}
func hasMountOption(options, value string) bool {
	for _, opt := range strings.Split(options, ",") {
		if opt == value {
			return true
		}
	}
	return false
}
func inspectStorageVolume(root, path string, mounts []storageMount) (v StorageVolume) {
	v.Path = path
	real, err := existingStoragePath(root, path)
	if err != nil {
		v.Problem = err.Error()
		return
	}
	var selected *storageMount
	for i := range mounts {
		m := &mounts[i]
		if (m.path == "/" || real == m.path || strings.HasPrefix(real, m.path+"/")) && (selected == nil || len(m.path) >= len(selected.path)) {
			selected = m
		}
	}
	if selected == nil {
		v.Problem = "no backing mount found"
		return
	}
	v.Mount = selected.path
	v.Filesystem = selected.fs
	if hasMountOption(selected.options, "ro") || hasMountOption(selected.superOptions, "ro") || !hasMountOption(selected.options, "rw") || !hasMountOption(selected.superOptions, "rw") {
		v.Problem = "filesystem is read-only or write state is unknown"
		return
	}
	// Only the current A133 ext4 baseline is implemented. Overlayfs and other
	// filesystems require their own provider inspection, never implicit success.
	if selected.fs != "ext4" {
		v.Problem = "unsupported storage filesystem: " + selected.fs
		return
	}
	parts := strings.Split(selected.device, ":")
	if len(parts) != 2 {
		v.Problem = "invalid block device identity"
		return
	}
	for _, p := range parts {
		if _, err := strconv.ParseUint(p, 10, 32); err != nil || p == "" {
			v.Problem = "invalid block device identity"
			return
		}
	}
	b, err := os.ReadFile(filepath.Join(root, "sys/dev/block", selected.device, "uevent"))
	if err != nil {
		v.Problem = "block identity unavailable: " + err.Error()
		return
	}
	for _, line := range strings.Split(string(b), "\n") {
		if strings.HasPrefix(line, "DEVNAME=") {
			if v.Device != "" {
				v.Problem = "ambiguous block device identity"
				return
			}
			v.Device = strings.TrimPrefix(line, "DEVNAME=")
		}
	}
	if !blockDeviceName.MatchString(v.Device) {
		v.Problem = "invalid block device name"
		return
	}
	b, err = os.ReadFile(filepath.Join(root, "sys/fs/ext4", v.Device, "errors_count"))
	if err != nil {
		v.Problem = "EXT4 error counter unavailable: " + err.Error()
		return
	}
	n, err := strconv.ParseUint(strings.TrimSpace(string(b)), 10, 64)
	if err != nil {
		v.Problem = "invalid EXT4 error counter"
		return
	}
	v.Errors = &n
	if n != 0 {
		v.Problem = fmt.Sprintf("EXT4 has %d error(s) since last filesystem check; offline maintenance required", n)
	}
	return
}
func inspectStorage(root, dataRoot string) (StorageReport, error) {
	report := StorageReport{Passed: true, Volumes: []StorageVolume{}}
	mounts, err := storageMounts(root)
	if err != nil {
		report.Passed = false
		return report, fmt.Errorf("storage preflight: %w", err)
	}
	var problems []string
	for _, p := range []string{"/", dataRoot} {
		v := inspectStorageVolume(root, p, mounts)
		report.Volumes = append(report.Volumes, v)
		if v.Problem != "" {
			problems = append(problems, p+": "+v.Problem)
		}
	}
	if len(problems) > 0 {
		report.Passed = false
		return report, fmt.Errorf("storage preflight: %s", strings.Join(problems, "; "))
	}
	return report, nil
}

// StorageHealth reads kernel metadata only; it never mounts, repairs or writes.
func StorageHealth(dataRoot string) (StorageReport, error) { return inspectStorage("/", dataRoot) }
