package kvm

import (
	"io"
	"os"
	"path/filepath"
	"testing"
	"testing/fstest"
)

func TestDesktopAssetsUseOneSelectedVersion(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, "index.html"), []byte("new desktop"), 0644)
	selected := ""
	assets := desktopAssetFS{fallback: fstest.MapFS{"index.html": {Data: []byte("factory")}}, selectRoot: func() string { return selected }}
	read := func() string {
		f, e := assets.Open("index.html")
		if e != nil {
			t.Fatal(e)
		}
		defer f.Close()
		b, e := io.ReadAll(f)
		if e != nil {
			t.Fatal(e)
		}
		return string(b)
	}
	if read() != "factory" {
		t.Fatal("factory fallback")
	}
	selected = root
	if read() != "new desktop" {
		t.Fatal("new assets unavailable")
	}
	if _, e := assets.Open("../index.html"); e == nil {
		t.Fatal("traversal")
	}
	os.Symlink("/etc/passwd", filepath.Join(root, "leak"))
	if _, e := assets.Open("leak"); e == nil {
		t.Fatal("symlink escape")
	}
	selected = ""
	if read() != "factory" {
		t.Fatal("rollback assets unavailable")
	}
}
