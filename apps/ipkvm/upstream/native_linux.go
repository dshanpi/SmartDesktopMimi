//go:build linux

package kvm

import (
	"fmt"
	"os"
	"os/exec"
	"sync"
	"syscall"
)

// nativeOutput writes output to a log file
type nativeOutput struct {
	mu   *sync.Mutex
	file *os.File
}

func newNativeOutput(path string) (*nativeOutput, error) {
	f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return nil, err
	}
	return &nativeOutput{
		mu:   &sync.Mutex{},
		file: f,
	}, nil
}

func (w *nativeOutput) Write(p []byte) (n int, err error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	// Write to file, skip very long lines
	if len(p) > 0 && len(p) < 4096 {
		w.file.Write(p)
	}
	return len(p), nil
}

func (w *nativeOutput) Close() error {
	if w.file != nil {
		return w.file.Close()
	}
	return nil
}

func startVideoBinary(binaryPath string) (*exec.Cmd, error) {
	// Run the binary in the background
	cmd := exec.Command(binaryPath)

	videoStdout, err := newNativeOutput("/tmp/kvm_video.log")
	if err != nil {
		return nil, err
	}
	videoStderr, err := newNativeOutput("/tmp/kvm_video.log")
	if err != nil {
		videoStdout.Close()
		return nil, err
	}

	// Redirect stdout and stderr to log file
	cmd.Stdout = videoStdout
	cmd.Stderr = videoStderr

	// Set the process group ID so we can kill the process and its children when this process exits
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid:   true,
		Pdeathsig: syscall.SIGKILL,
	}

	// Start the command
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start binary: %w", err)
	}

	return cmd, nil
}

func startAudioBinary(binaryPath string) (*exec.Cmd, error) {
	// Run the binary in the background
	cmd := exec.Command(binaryPath)

	audioOutputLock := sync.Mutex{}
	audioStdout := &nativeOutput{
		mu: &audioOutputLock,
	}
	audioStderr := &nativeOutput{
		mu: &audioOutputLock,
	}

	// Redirect stdout and stderr to discard
	cmd.Stdout = audioStdout
	cmd.Stderr = audioStderr

	// Set the process group ID so we can kill the process and its children when this process exits
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid:   true,
		Pdeathsig: syscall.SIGKILL,
	}

	// Start the command
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start binary: %w", err)
	}

	return cmd, nil
}

func startVpnBinary(binaryPath string) (*exec.Cmd, error) {
	// Run the binary in the background
	cmd := exec.Command(binaryPath)

	vpnOutputLock := sync.Mutex{}
	vpnStdout := &nativeOutput{
		mu: &vpnOutputLock,
	}
	vpnStderr := &nativeOutput{
		mu: &vpnOutputLock,
	}

	// Redirect stdout and stderr to discard
	cmd.Stdout = vpnStdout
	cmd.Stderr = vpnStderr

	// Set the process group ID so we can kill the process and its children when this process exits
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid:   true,
		Pdeathsig: syscall.SIGKILL,
	}

	// Start the command
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start binary: %w", err)
	}

	return cmd, nil
}

func startDisplayBinary(binaryPath string) (*exec.Cmd, error) {
	// Run the binary in the background
	cmd := exec.Command(binaryPath)

	displayOutputLock := sync.Mutex{}
	displayStdout := &nativeOutput{
		mu: &displayOutputLock,
	}
	displayStderr := &nativeOutput{
		mu: &displayOutputLock,
	}

	// Redirect stdout and stderr to discard
	cmd.Stdout = displayStdout
	cmd.Stderr = displayStderr

	// Set the process group ID so we can kill the process and its children when this process exits
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid:   true,
		Pdeathsig: syscall.SIGKILL,
	}

	// Start the command
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start binary: %w", err)
	}

	return cmd, nil
}
