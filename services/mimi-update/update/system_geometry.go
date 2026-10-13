package update

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// A133Geometry reads the kernel's actual partition geometry. The digest format
// is fixed by this function; a filename or copied platform JSON is not evidence.
func A133Geometry() (string, int64, error) { return a133Geometry("/") }
func a133Geometry(root string) (string, int64, error) {
	type part struct {
		Name   string `json:"name"`
		Device string `json:"device"`
		Start  uint64 `json:"start_sector"`
		Size   uint64 `json:"sectors"`
	}
	read := func(path string) (uint64, error) {
		b, e := os.ReadFile(filepath.Join(root, path))
		if e != nil {
			return 0, e
		}
		n, e := strconv.ParseUint(strings.TrimSpace(string(b)), 10, 64)
		return n, e
	}
	sectors, err := read("sys/class/block/mmcblk0/size")
	if err != nil {
		return "", 0, err
	}
	if sectors == 0 || sectors > 1<<44 {
		return "", 0, fmt.Errorf("invalid eMMC capacity")
	}
	parts := []part{}
	for i, name := range []string{"bootA", "rootfsA", "bootB", "rootfsB"} {
		expected := fmt.Sprintf("mmcblk0p%d", i+3)
		resolved, err := filepath.EvalSymlinks(filepath.Join(root, "dev/by-name", name))
		if err != nil {
			return "", 0, err
		}
		if resolved != filepath.Join(root, "dev", expected) {
			return "", 0, fmt.Errorf("unexpected A/B partition mapping")
		}
		start, err := read("sys/class/block/" + expected + "/start")
		if err != nil {
			return "", 0, err
		}
		size, err := read("sys/class/block/" + expected + "/size")
		if err != nil {
			return "", 0, err
		}
		if size == 0 || start >= sectors || size > sectors-start {
			return "", 0, fmt.Errorf("invalid partition geometry")
		}
		if len(parts) > 0 && start < parts[len(parts)-1].Start+parts[len(parts)-1].Size {
			return "", 0, fmt.Errorf("overlapping partitions")
		}
		parts = append(parts, part{name, expected, start, size})
	}
	b, _ := json.Marshal(parts)
	sum := sha256.Sum256(b)
	return hex.EncodeToString(sum[:]), int64(sectors * 512), nil
}
