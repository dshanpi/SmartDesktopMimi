package kvm

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"syscall"
	"time"
)

type TailScaleSettings struct {
	State    string `json:"state"`
	LoginUrl string `json:"loginUrl"`
	IP       string `json:"ip"`
	XEdge    bool   `json:"xEdge"`
}

type ZeroTierSettings struct {
	State     string `json:"state"`
	NetworkID string `json:"networkID"`
	IP        string `json:"ip"`
}

func rpcCancelTailScale() error {
	// VPN disabled for T527
	return nil
}

func rpcLoginTailScale(xEdge bool) (TailScaleSettings, error) {
	// VPN disabled for T527
	return TailScaleSettings{
		State:    "disabled",
		XEdge:    xEdge,
		LoginUrl: "",
		IP:       "",
	}, nil
}

func rpcLogoutTailScale() error {
	// VPN disabled for T527
	return nil
}

func rpcGetTailScaleSettings() (TailScaleSettings, error) {
	// VPN disabled for T527
	return TailScaleSettings{
		State:    "disabled",
		LoginUrl: "",
		IP:       "",
		XEdge:    false,
	}, nil
}

func rpcLoginZeroTier(networkID string) (ZeroTierSettings, error) {
	// VPN disabled for T527
	return ZeroTierSettings{
		State:     "disabled",
		NetworkID: networkID,
		IP:        "",
	}, nil
}

func rpcLogoutZeroTier(networkID string) error {
	// VPN disabled for T527
	return nil
}

func rpcGetZeroTierSettings() (ZeroTierSettings, error) {
	// VPN disabled for T527
	return ZeroTierSettings{
		State:     "disabled",
		NetworkID: "",
		IP:        "",
	}, nil
}

type VpnUpdateDisplayState struct {
	TailScaleState string `json:"tailscale_state"`
	ZeroTierState  string `json:"zerotier_state"`
	Error          string `json:"error,omitempty"` //no_signal, no_lock, out_of_range
}

func HandleVpnDisplayUpdateMessage(event CtrlResponse) {
	waitDisplayUpdate.Lock()
	defer waitDisplayUpdate.Unlock()
	waitDisplayCtrlClientConnected()

	vpnUpdateDisplayState := VpnUpdateDisplayState{}
	err := json.Unmarshal(event.Data, &vpnUpdateDisplayState)
	if err != nil {
		vpnLogger.Warn().Err(err).Msg("Error parsing vpn state json")
		return
	}

	switch vpnUpdateDisplayState.TailScaleState {
	case "connected":
		updateLabelIfChanged("Network_TailScale_Label", "Connected")
	case "logined":
		updateLabelIfChanged("Network_TailScale_Label", "Logined")
	default:
		updateLabelIfChanged("Network_TailScale_Label", "Disconnected")
	}

	switch vpnUpdateDisplayState.ZeroTierState {
	case "connected":
		updateLabelIfChanged("Network_ZeroTier_Label", "Connected")
	case "logined":
		updateLabelIfChanged("Network_ZeroTier_Label", "Logined")
	default:
		updateLabelIfChanged("Network_ZeroTier_Label", "Disconnected")
	}
}

type FrpcStatus struct {
	Running bool `json:"running"`
}

var (
	frpcTomlPath = envOrDefault("AITVBOX_FRPC_CONFIG", "/userdata/frpc/frpc.ini")
	frpcLogPath  = envOrDefault("AITVBOX_FRPC_LOG", "/tmp/frpc.log")

	frpcOperationMu sync.Mutex
	frpcProcessMu   sync.Mutex
	frpcProcess     *managedFrpcProcess
)

type managedFrpcProcess struct {
	cmd     *exec.Cmd
	done    chan struct{}
	waitErr error
}

func frpcRunning() bool {
	frpcProcessMu.Lock()
	process := frpcProcess
	frpcProcessMu.Unlock()
	if process == nil {
		return false
	}
	select {
	case <-process.done:
		return false
	default:
		return process.cmd.Process.Signal(syscall.Signal(0)) == nil
	}
}

func stopManagedFrpc() error {
	frpcProcessMu.Lock()
	process := frpcProcess
	frpcProcess = nil
	frpcProcessMu.Unlock()
	if process == nil {
		return nil
	}

	select {
	case <-process.done:
		return nil
	default:
	}
	if err := process.cmd.Process.Signal(syscall.SIGTERM); err != nil &&
		!errors.Is(err, os.ErrProcessDone) {
		return err
	}
	select {
	case <-process.done:
		return nil
	case <-time.After(2 * time.Second):
	}
	if err := process.cmd.Process.Kill(); err != nil && !errors.Is(err, os.ErrProcessDone) {
		return err
	}
	<-process.done
	return nil
}

func stopManagedFrpcForShutdown() error {
	frpcOperationMu.Lock()
	defer frpcOperationMu.Unlock()
	return stopManagedFrpc()
}

func rpcGetFrpcLog() (string, error) {
	f, err := os.Open(frpcLogPath)
	if err != nil {
		if os.IsNotExist(err) {
			return "", fmt.Errorf("frpc log file not exist")
		}
		return "", err
	}
	defer f.Close()

	const want = 30
	lines := make([]string, 0, want+10)
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		lines = append(lines, sc.Text())
		if len(lines) > want {
			lines = lines[1:]
		}
	}
	if err := sc.Err(); err != nil {
		return "", err
	}

	var buf []byte
	for _, l := range lines {
		buf = append(buf, l...)
		buf = append(buf, '\n')
	}
	return string(buf), nil
}

func rpcGetFrpcToml() (string, error) {
	return config.FrpcToml, nil
}

func rpcStartFrpc(frpcToml string) error {
	frpcOperationMu.Lock()
	defer frpcOperationMu.Unlock()

	if len(frpcToml) == 0 || len(frpcToml) > 64*1024 {
		return fmt.Errorf("frpc configuration must be between 1 byte and 64 KiB")
	}
	if err := stopManagedFrpc(); err != nil {
		return fmt.Errorf("stop previous frpc process: %w", err)
	}

	if frpcToml != "" {
		configDir := filepath.Dir(frpcTomlPath)
		if err := os.MkdirAll(configDir, 0700); err != nil {
			return err
		}
		if err := os.Chmod(configDir, 0700); err != nil {
			return err
		}
		if err := os.WriteFile(frpcTomlPath, []byte(frpcToml), 0600); err != nil {
			return err
		}
		if err := os.Chmod(frpcTomlPath, 0600); err != nil {
			return err
		}
		cmd := exec.Command("frpc", "-c", frpcTomlPath)
		cmd.Stdout = nil
		cmd.Stderr = nil
		logFile, err := os.OpenFile(frpcLogPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0600)
		if err != nil {
			return err
		}
		defer logFile.Close()
		if err := logFile.Chmod(0600); err != nil {
			return err
		}
		cmd.Stdout = logFile
		cmd.Stderr = logFile

		cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}

		if err := cmd.Start(); err != nil {
			return fmt.Errorf("start frpc failed: %w", err)
		} else {
			process := &managedFrpcProcess{cmd: cmd, done: make(chan struct{})}
			frpcProcessMu.Lock()
			frpcProcess = process
			frpcProcessMu.Unlock()
			go func() {
				process.waitErr = cmd.Wait()
				close(process.done)
			}()
			select {
			case <-process.done:
				frpcProcessMu.Lock()
				if frpcProcess == process {
					frpcProcess = nil
				}
				frpcProcessMu.Unlock()
				if process.waitErr != nil {
					return fmt.Errorf("frpc exited during startup: %w", process.waitErr)
				}
				return fmt.Errorf("frpc exited during startup")
			case <-time.After(150 * time.Millisecond):
			}

			config.FrpcAutoStart = true
			config.FrpcToml = frpcToml
			if err := SaveConfig(); err != nil {
				_ = stopManagedFrpc()
				return fmt.Errorf("failed to save config: %w", err)
			}
		}
	} else {
		return fmt.Errorf("frpcToml is empty")
	}

	return nil
}

func rpcStopFrpc() error {
	frpcOperationMu.Lock()
	defer frpcOperationMu.Unlock()

	if err := stopManagedFrpc(); err != nil {
		return fmt.Errorf("failed to stop frpc: %w", err)
	}

	config.FrpcAutoStart = false
	err := SaveConfig()
	if err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}
	return nil
}

func rpcGetFrpcStatus() (FrpcStatus, error) {
	return FrpcStatus{Running: frpcRunning()}, nil
}

type CloudflaredStatus struct {
	Running bool `json:"running"`
}

func cloudflaredRunning() bool {
	cmd := exec.Command("pgrep", "-x", "cloudflared")
	return cmd.Run() == nil
}

var (
	cloudflaredLogPath = "/tmp/cloudflared.log"
)

func rpcStartCloudflared(token string) error {
	if cloudflaredRunning() {
		_ = exec.Command("pkill", "-x", "cloudflared").Run()
	}
	if token == "" {
		return fmt.Errorf("cloudflared token is empty")
	}
	cmd := exec.Command("cloudflared", "tunnel", "run", "--token", token)
	logFile, err := os.OpenFile(cloudflaredLogPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return err
	}
	defer logFile.Close()
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start cloudflared failed: %w", err)
	}
	config.CloudflaredAutoStart = true
	config.CloudflaredToken = token
	if err := SaveConfig(); err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}
	return nil
}

func rpcStopCloudflared() error {
	if cloudflaredRunning() {
		err := exec.Command("pkill", "-x", "cloudflared").Run()
		if err != nil {
			return fmt.Errorf("failed to stop cloudflared: %w", err)
		}
	}
	config.CloudflaredAutoStart = false
	if err := SaveConfig(); err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}
	return nil
}

func rpcGetCloudflaredStatus() (CloudflaredStatus, error) {
	return CloudflaredStatus{Running: cloudflaredRunning()}, nil
}

func rpcGetCloudflaredLog() (string, error) {
	f, err := os.Open(cloudflaredLogPath)
	if err != nil {
		if os.IsNotExist(err) {
			return "", fmt.Errorf("cloudflared log file not exist")
		}
		return "", err
	}
	defer f.Close()

	const want = 30
	lines := make([]string, 0, want+10)
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		lines = append(lines, sc.Text())
		if len(lines) > want {
			lines = lines[1:]
		}
	}
	if err := sc.Err(); err != nil {
		return "", err
	}

	var buf []byte
	for _, l := range lines {
		buf = append(buf, l...)
		buf = append(buf, '\n')
	}
	return string(buf), nil
}

type EasytierStatus struct {
	Running bool `json:"running"`
}

type EasytierConfig struct {
	Name   string `json:"name"`
	Secret string `json:"secret"`
	Node   string `json:"node"`
}

var (
	easytierLogPath = "/tmp/easytier.log"
)

func easytierRunning() bool {
	cmd := exec.Command("pgrep", "-x", "easytier-core")
	return cmd.Run() == nil
}

func rpcGetEasyTierLog() (string, error) {
	f, err := os.Open(easytierLogPath)
	if err != nil {
		if os.IsNotExist(err) {
			return "", fmt.Errorf("easytier log file not exist")
		}
		return "", err
	}
	defer f.Close()

	const want = 30
	lines := make([]string, 0, want+10)
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		lines = append(lines, sc.Text())
		if len(lines) > want {
			lines = lines[1:]
		}
	}
	if err := sc.Err(); err != nil {
		return "", err
	}

	var buf []byte
	for _, l := range lines {
		buf = append(buf, l...)
		buf = append(buf, '\n')
	}
	return string(buf), nil
}

func rpcGetEasyTierNodeInfo() (string, error) {
	cmd := exec.Command("easytier-cli", "node")
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to get easytier node info: %w", err)
	}

	return string(output), nil
}

func rpcGetEasyTierConfig() (EasytierConfig, error) {
	return config.EasytierConfig, nil
}

func rpcStartEasyTier(name, secret, node string) error {
	if easytierRunning() {
		_ = exec.Command("pkill", "-x", "easytier-core").Run()
	}

	if name == "" || secret == "" || node == "" {
		return fmt.Errorf("easytier config is invalid")
	}

	cmd := exec.Command("easytier-core", "-d", "--network-name", name, "--network-secret", secret, "-p", node)
	cmd.Stdout = nil
	cmd.Stderr = nil
	logFile, err := os.OpenFile(easytierLogPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return fmt.Errorf("failed to open easytier log file: %w", err)
	}
	defer logFile.Close()
	cmd.Stdout = logFile
	cmd.Stderr = logFile

	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start easytier failed: %w", err)
	} else {
		config.EasytierAutoStart = true
		config.EasytierConfig = EasytierConfig{
			Name:   name,
			Secret: secret,
			Node:   node,
		}
		if err := SaveConfig(); err != nil {
			return fmt.Errorf("failed to save config: %w", err)
		}
	}

	return nil
}

func rpcStopEasyTier() error {
	if easytierRunning() {
		err := exec.Command("pkill", "-x", "easytier-core").Run()
		if err != nil {
			return fmt.Errorf("failed to stop easytier: %w", err)
		}
	}

	config.EasytierAutoStart = false
	err := SaveConfig()
	if err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}
	return nil
}

func rpcGetEasyTierStatus() (EasytierStatus, error) {
	return EasytierStatus{Running: easytierRunning()}, nil
}

type VntStatus struct {
	Running bool `json:"running"`
}

var (
	vntLogPath        = "/tmp/vnt.log"
	vntConfigFilePath = "/userdata/vnt/vnt.ini"
)

func vntRunning() bool {
	cmd := exec.Command("pgrep", "-x", "vnt-cli")
	return cmd.Run() == nil
}

func rpcGetVntLog() (string, error) {
	f, err := os.Open(vntLogPath)
	if err != nil {
		if os.IsNotExist(err) {
			return "", fmt.Errorf("vnt log file not exist")
		}
		return "", err
	}
	defer f.Close()

	const want = 30
	lines := make([]string, 0, want+10)
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		lines = append(lines, sc.Text())
		if len(lines) > want {
			lines = lines[1:]
		}
	}
	if err := sc.Err(); err != nil {
		return "", err
	}

	var buf []byte
	for _, l := range lines {
		buf = append(buf, l...)
		buf = append(buf, '\n')
	}
	return string(buf), nil
}

func rpcGetVntInfo() (string, error) {
	cmd := exec.Command("vnt-cli", "--info")
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to get vnt info: %w", err)
	}

	return string(output), nil
}

func rpcGetVntConfig() (VntConfig, error) {
	return config.VntConfig, nil
}

func rpcGetVntConfigFile() (string, error) {
	return config.VntConfig.ConfigFile, nil
}

func rpcStartVnt(configMode, token, deviceId, name, serverAddr, configFile string, model string, password string) error {
	if vntRunning() {
		_ = exec.Command("pkill", "-x", "vnt-cli").Run()
	}

	var args []string

	if configMode == "file" {
		// Use config file mode
		if configFile == "" {
			return fmt.Errorf("vnt config file is required in file mode")
		}

		// Save config file
		_ = os.MkdirAll(filepath.Dir(vntConfigFilePath), 0700)
		if err := os.WriteFile(vntConfigFilePath, []byte(configFile), 0600); err != nil {
			return fmt.Errorf("failed to write vnt config file: %w", err)
		}

		args = []string{"-f", vntConfigFilePath}
	} else {
		// Use params mode (default)
		if token == "" {
			return fmt.Errorf("vnt token is required in params mode")
		}

		args = []string{"-k", token}

		if deviceId != "" {
			args = append(args, "-d", deviceId)
		}

		if name != "" {
			args = append(args, "-n", name)
		}

		if serverAddr != "" {
			args = append(args, "-s", serverAddr)
		}

		// Encryption model and password
		if model != "" {
			args = append(args, "--model", model)
		}
		if password != "" {
			args = append(args, "-w", password)
		}

		args = append(args, "--compressor", "lz4")
	}

	cmd := exec.Command("vnt-cli", args...)
	cmd.Stdout = nil
	cmd.Stderr = nil
	logFile, err := os.OpenFile(vntLogPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return fmt.Errorf("failed to open vnt log file: %w", err)
	}
	defer logFile.Close()
	cmd.Stdout = logFile
	cmd.Stderr = logFile

	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start vnt failed: %w", err)
	} else {
		config.VntAutoStart = true
		config.VntConfig = VntConfig{
			ConfigMode: configMode,
			Token:      token,
			DeviceId:   deviceId,
			Name:       name,
			ServerAddr: serverAddr,
			ConfigFile: configFile,
			Model:      model,
			Password:   password,
		}
		if err := SaveConfig(); err != nil {
			return fmt.Errorf("failed to save config: %w", err)
		}
	}

	return nil
}

func rpcStopVnt() error {
	if vntRunning() {
		err := exec.Command("pkill", "-x", "vnt-cli").Run()
		if err != nil {
			return fmt.Errorf("failed to stop vnt: %w", err)
		}
	}

	config.VntAutoStart = false
	err := SaveConfig()
	if err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}
	return nil
}

func rpcGetVntStatus() (VntStatus, error) {
	return VntStatus{Running: vntRunning()}, nil
}

type WireguardStatus struct {
	Running bool `json:"running"`
}

var (
	wireguardLogPath  = "/tmp/wireguard.log"
	wireguardConfPath = "/etc/wireguard/wg0.conf"
)

func wireguardRunning() bool {
	cmd := exec.Command("ip", "link", "show", "wg0")
	return cmd.Run() == nil
}

func rpcGetWireguardLog() (string, error) {
	f, err := os.Open(wireguardLogPath)
	if err != nil {
		if os.IsNotExist(err) {
			return "", fmt.Errorf("wireguard log file not exist")
		}
		return "", err
	}
	defer f.Close()

	const want = 30
	lines := make([]string, 0, want+10)
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		lines = append(lines, sc.Text())
		if len(lines) > want {
			lines = lines[1:]
		}
	}
	if err := sc.Err(); err != nil {
		return "", err
	}

	var buf []byte
	for _, l := range lines {
		buf = append(buf, l...)
		buf = append(buf, '\n')
	}
	return string(buf), nil
}

func rpcGetWireguardConfig() (WireguardConfig, error) {
	return config.WireguardConfig, nil
}

func rpcStartWireguard(configFile string) error {
	if wireguardRunning() {
		_ = exec.Command("wg-quick", "down", wireguardConfPath).Run()
	}

	if configFile == "" {
		return fmt.Errorf("wireguard config file is required")
	}

	_ = os.MkdirAll(filepath.Dir(wireguardConfPath), 0700)
	if err := os.WriteFile(wireguardConfPath, []byte(configFile), 0600); err != nil {
		return fmt.Errorf("failed to write wireguard config file: %w", err)
	}

	cmd := exec.Command("wg-quick", "up", wireguardConfPath)
	logFile, err := os.OpenFile(wireguardLogPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return fmt.Errorf("failed to open wireguard log file: %w", err)
	}
	defer logFile.Close()
	cmd.Stdout = logFile
	cmd.Stderr = logFile

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("start wireguard failed: %w", err)
	}

	config.WireguardAutoStart = true
	config.WireguardConfig.ConfigFile = configFile
	if err := SaveConfig(); err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}

	return nil
}

func rpcStopWireguard() error {
	if wireguardRunning() {
		cmd := exec.Command("wg-quick", "down", wireguardConfPath)
		logFile, err := os.OpenFile(wireguardLogPath, os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0644)
		if err == nil {
			defer logFile.Close()
			cmd.Stdout = logFile
			cmd.Stderr = logFile
		}

		if err := cmd.Run(); err != nil {
			return fmt.Errorf("failed to stop wireguard: %w", err)
		}
	}

	config.WireguardAutoStart = false
	if err := SaveConfig(); err != nil {
		return fmt.Errorf("failed to save config: %w", err)
	}
	return nil
}

func rpcGetWireguardStatus() (WireguardStatus, error) {
	return WireguardStatus{Running: wireguardRunning()}, nil
}

func rpcGetWireguardInfo() (string, error) {
	cmd := exec.Command("wg", "show")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("failed to get wireguard info: %w", err)
	}
	return string(output), nil
}

func initVPN() {
	// VPN disabled for T527 - no-op
	vpnLogger.Info().Msg("VPN disabled for T527")
}
