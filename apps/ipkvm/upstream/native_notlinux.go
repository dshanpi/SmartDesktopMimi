//go:build !linux

package kvm

import (
	"fmt"
	"os/exec"
)

func startVideoBinary(binaryPath string) (*exec.Cmd, error) {
	return nil, fmt.Errorf("not supported")
}

func startAudioBinary(binaryPath string) (*exec.Cmd, error) {
	return nil, fmt.Errorf("not supported")
}

func startVpnBinary(binaryPath string) (*exec.Cmd, error) {
	return nil, fmt.Errorf("not supported")
}

func startDisplayBinary(binaryPath string) (*exec.Cmd, error) {
	return nil, fmt.Errorf("not supported")
}
