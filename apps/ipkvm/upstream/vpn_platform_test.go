package kvm

import (
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"testing"
)

func TestPlatformFrpcOnlyStopsManagedProcess(t *testing.T) {
	root := t.TempDir()
	fakeFrpc := filepath.Join(root, "frpc")
	if err := os.WriteFile(fakeFrpc, []byte("#!/bin/sh\nexec sleep 60\n"), 0700); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", root+string(os.PathListSeparator)+os.Getenv("PATH"))

	oldConfigPath, oldFrpcPath, oldLogPath := configPath, frpcTomlPath, frpcLogPath
	configPath = filepath.Join(root, "config.json")
	frpcTomlPath = filepath.Join(root, "frpc.ini")
	frpcLogPath = filepath.Join(root, "frpc.log")
	config = nil
	LoadConfig()
	t.Cleanup(func() {
		_ = stopManagedFrpc()
		config = nil
		configPath, frpcTomlPath, frpcLogPath = oldConfigPath, oldFrpcPath, oldLogPath
	})

	unrelated := exec.Command(fakeFrpc)
	if err := unrelated.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_ = unrelated.Process.Kill()
		_ = unrelated.Wait()
	})

	if err := rpcStartFrpc("[common]\nserver_addr = 127.0.0.1\n"); err != nil {
		t.Fatal(err)
	}
	if !frpcRunning() {
		t.Fatal("managed FRPC process is not running")
	}
	if err := rpcStopFrpc(); err != nil {
		t.Fatal(err)
	}
	if frpcRunning() {
		t.Fatal("managed FRPC process is still running")
	}
	if err := unrelated.Process.Signal(syscall.Signal(0)); err != nil {
		t.Fatalf("unrelated process was stopped: %v", err)
	}

	for _, path := range []string{configPath, frpcTomlPath, frpcLogPath} {
		info, err := os.Stat(path)
		if err != nil {
			t.Fatal(err)
		}
		if info.Mode().Perm() != 0600 {
			t.Fatalf("%s mode = %o, want 600", path, info.Mode().Perm())
		}
	}
}

func TestPlatformFrpcRejectsImmediateExit(t *testing.T) {
	root := t.TempDir()
	fakeFrpc := filepath.Join(root, "frpc")
	if err := os.WriteFile(fakeFrpc, []byte("#!/bin/sh\nexit 7\n"), 0700); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", root+string(os.PathListSeparator)+os.Getenv("PATH"))

	oldConfigPath, oldFrpcPath, oldLogPath := configPath, frpcTomlPath, frpcLogPath
	configPath = filepath.Join(root, "config.json")
	frpcTomlPath = filepath.Join(root, "frpc.ini")
	frpcLogPath = filepath.Join(root, "frpc.log")
	config = nil
	LoadConfig()
	t.Cleanup(func() {
		_ = stopManagedFrpcForShutdown()
		config = nil
		configPath, frpcTomlPath, frpcLogPath = oldConfigPath, oldFrpcPath, oldLogPath
	})

	if err := rpcStartFrpc("[common]\nserver_addr = 127.0.0.1\n"); err == nil {
		t.Fatal("immediately exiting FRPC process was accepted")
	}
	if frpcRunning() {
		t.Fatal("failed FRPC process is reported as running")
	}
	if config.FrpcAutoStart {
		t.Fatal("failed FRPC process enabled autostart")
	}
}
