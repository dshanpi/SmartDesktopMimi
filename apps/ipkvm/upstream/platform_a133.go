package kvm

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"net"
	"os"
	"os/signal"
	"strconv"
	"sync"
	"syscall"
	"time"

	"github.com/pion/ice/v4"
)

const defaultHDMIOwnerPath = "/var/run/aitvbox/hdmi-ipkvm.lock"

var (
	platformVideoMu     sync.Mutex
	platformVideoUsers  int
	platformVideoLive   bool
	platformOwnsHDMI    bool
	platformAgentHDMI   bool
	platformWebRTCMu    sync.Mutex
	platformICEUDPMux   *ice.MultiUDPMuxDefault
	platformVideoAction = writeCtrlAction
)

func isPlatformMode() bool {
	return os.Getenv("AITVBOX_PLATFORM_MODE") == "1"
}

func MainPlatform() {
	logger.Info().Str("version", buildVersion).Msg("A133 IPKVM service starting")
	LoadConfig()
	if err := enforcePlatformAuthentication(); err != nil {
		logger.Error().Err(err).Msg("cannot initialize platform authentication")
		return
	}
	// The generic USB watcher is not started in A133 platform mode. Seed the
	// session event with the real composite-HID state instead of the generic
	// package default ("unknown"), otherwise the browser correctly refuses to
	// forward input even while the UDC is configured.
	usbState = rpcGetUSBState()

	var cancel context.CancelFunc
	appCtx, cancel = context.WithCancel(context.Background())
	defer cancel()

	if err := StartVideoCtrlSocketServer(); err != nil {
		logger.Error().Err(err).Msg("cannot initialize video control socket")
		return
	}
	if err := StartVideoDataSocketServer(); err != nil {
		logger.Error().Err(err).Msg("cannot initialize video data socket")
		closePlatformListeners()
		return
	}
	if err := startPlatformWebRTC(); err != nil {
		logger.Error().Err(err).Msg("cannot initialize fixed-port WebRTC")
		return
	}
	defer closePlatformWebRTC()
	// Platform capture ownership is driven by actual ICE sessions and Agent
	// references. The generic broadcaster callbacks can otherwise count the
	// same browser twice and leave capture permanently live after disconnect.
	videoBroadcaster.onFirstSubscribe = nil
	videoBroadcaster.onLastUnsubscribe = nil
	initPrometheus()

	if config.FrpcAutoStart && config.FrpcToml != "" {
		go func() {
			if err := rpcStartFrpc(config.FrpcToml); err != nil {
				logger.Warn().Err(err).Msg("failed to auto-start FRPC")
			}
		}()
	}

	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM)
	defer signal.Stop(sigs)
	webServerDone := make(chan struct{})
	go func() {
		RunWebServer()
		close(webServerDone)
	}()
	select {
	case <-sigs:
	case <-webServerDone:
		logger.Error().Msg("IPKVM web server stopped unexpectedly")
	}
	cancel()
	platformForceStopVideo()
	_ = stopManagedFrpcForShutdown()
	closePlatformListeners()
	_ = platformReleaseHID()
	logger.Info().Msg("A133 IPKVM service stopped")
}

func enforcePlatformAuthentication() error {
	if configLoadError != nil {
		return configLoadError
	}
	if config == nil {
		return errors.New("IPKVM configuration is not loaded")
	}
	if config.LocalAuthMode != "password" || config.HashedPassword == "" {
		config.LocalAuthMode = ""
		config.HashedPassword = ""
		config.LocalAuthToken = ""
	}
	// Keep the active browser session token across a supervised web-service
	// restart. The browser cookie is still session-scoped, logout clears the
	// durable token, and every successful login rotates it.
	return SaveConfig()
}

func startPlatformWebRTC() error {
	port, err := platformWebRTCPort()
	if err != nil {
		return err
	}
	publicIP := os.Getenv("AITVBOX_WEBRTC_PUBLIC_IP")
	if publicIP != "" && net.ParseIP(publicIP) == nil {
		return errors.New("AITVBOX_WEBRTC_PUBLIC_IP must be a numeric IPv4 or IPv6 address")
	}
	logger.Info().Int("port", port).Str("publicIP", publicIP).
		Msg("fixed-port WebRTC configured; listener will follow active network interfaces")
	return nil
}

func platformWebRTCPort() (int, error) {
	portText := envOrDefault("AITVBOX_WEBRTC_UDP_PORT", "40000")
	port, err := strconv.Atoi(portText)
	if err != nil || port < 1 || port > 65535 {
		return 0, errors.New("AITVBOX_WEBRTC_UDP_PORT must be between 1 and 65535")
	}
	return port, nil
}

func refreshPlatformICEUDPMux() (*ice.MultiUDPMuxDefault, error) {
	platformWebRTCMu.Lock()
	defer platformWebRTCMu.Unlock()

	// MultiUDPMux snapshots the interface addresses when it is constructed.
	// Wi-Fi is commonly configured after ipkvmd starts, so construct the mux
	// when a browser actually opens a session. Replacing the current mux is
	// safe because this product permits only one active WebRTC session.
	if platformICEUDPMux != nil {
		_ = platformICEUDPMux.Close()
		platformICEUDPMux = nil
	}
	port, err := platformWebRTCPort()
	if err != nil {
		return nil, err
	}
	mux, err := ice.NewMultiUDPMuxFromPort(
		port,
		ice.UDPMuxFromPortWithLoopback(),
		ice.UDPMuxFromPortWithNetworks(ice.NetworkTypeUDP4, ice.NetworkTypeUDP6),
	)
	if err != nil {
		return nil, err
	}
	platformICEUDPMux = mux
	return mux, nil
}

func closePlatformWebRTC() {
	platformWebRTCMu.Lock()
	defer platformWebRTCMu.Unlock()
	if platformICEUDPMux != nil {
		_ = platformICEUDPMux.Close()
		platformICEUDPMux = nil
	}
}

func closePlatformListeners() {
	if videoCtrlSocketListener != nil {
		_ = videoCtrlSocketListener.Close()
		videoCtrlSocketListener = nil
	}
	if videoSocketListener != nil {
		_ = videoSocketListener.Close()
		videoSocketListener = nil
	}
}

func platformStartVideo() {
	platformVideoMu.Lock()
	defer platformVideoMu.Unlock()
	platformVideoUsers++
	if platformVideoLive {
		if err := platformVideoAction("start_video"); err != nil {
			videoLogger.Warn().Err(err).Msg("cannot request a keyframe for new IPKVM session")
		}
		return
	}
	if err := platformAcquireHDMI(); err != nil {
		platformVideoUsers--
		videoLogger.Error().Err(err).Msg("cannot acquire HDMI input for IPKVM")
		return
	}
	if err := platformWriteVideoStart(); err != nil {
		platformVideoUsers--
		platformReleaseHDMI()
		videoLogger.Error().Err(err).Msg("cannot start IPKVM video")
		return
	}
	platformVideoLive = true
}

func platformRequestVideoKeyFrame() error {
	platformVideoMu.Lock()
	defer platformVideoMu.Unlock()
	if !platformVideoLive {
		return nil
	}
	return platformVideoAction("start_video")
}

func platformWriteVideoStart() error {
	var err error
	for attempt := 0; attempt < 20; attempt++ {
		err = platformVideoAction("start_video")
		if err == nil {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	return err
}

func platformStopVideo() {
	platformVideoMu.Lock()
	defer platformVideoMu.Unlock()
	if platformVideoUsers > 0 {
		platformVideoUsers--
	}
	if platformVideoUsers == 0 && platformVideoLive {
		// LT6911/VIN can wedge the entire A133 after repeated
		// STREAMOFF/STREAMON cycles. Keep the single capture pipeline alive
		// across browser and Agent sessions; idle sessions simply have no
		// subscribers. Service shutdown performs the one ordered stop.
		videoLogger.Debug().Msg("IPKVM video idle; keeping capture resident")
	}
}

func platformForceStopVideo() {
	platformVideoMu.Lock()
	defer platformVideoMu.Unlock()
	platformVideoUsers = 0
	if platformVideoLive {
		_ = platformVideoAction("stop_video")
		platformVideoLive = false
	}
	platformReleaseHDMI()
}

func platformAcquireHDMI() error {
	if platformOwnsHDMI {
		return nil
	}
	if platformAgentHDMI {
		return errors.New("HDMI input is reserved for computer control")
	}
	if os.Getenv("AITVBOX_SHARED_CAPTURE") == "1" {
		// The native capture daemon is the sole /dev/video0 owner and fans the
		// same repaired frame to local display, WebRTC and snapshots. Browser
		// sessions no longer stop the local preview or create an owner file.
		return nil
	}
	path := envOrDefault("AITVBOX_HDMI_OWNER_FILE", defaultHDMIOwnerPath)
	if err := os.MkdirAll(dirName(path), 0755); err != nil {
		return err
	}

	for attempt := 0; attempt < 2; attempt++ {
		file, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0644)
		if err == nil {
			_, writeErr := file.WriteString(strconv.Itoa(os.Getpid()) + "\n")
			closeErr := file.Close()
			if writeErr != nil {
				_ = os.Remove(path)
				return writeErr
			}
			if closeErr != nil {
				_ = os.Remove(path)
				return closeErr
			}
			platformOwnsHDMI = true
			if waitErr := platformWaitForPreviewRelease(6 * time.Second); waitErr != nil {
				_ = os.Remove(path)
				platformOwnsHDMI = false
				return waitErr
			}
			return nil
		}
		if !errors.Is(err, os.ErrExist) {
			return err
		}
		owner, readErr := os.ReadFile(path)
		if readErr != nil {
			return err
		}
		pid, parseErr := strconv.Atoi(stringTrimSpace(owner))
		if parseErr == nil && processAlive(pid) {
			return errors.New("HDMI input is owned by another service")
		}
		if removeErr := os.Remove(path); removeErr != nil {
			return removeErr
		}
	}
	return errors.New("failed to acquire HDMI input")
}

func platformWaitForPreviewRelease(timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for {
		running, err := platformPreviewRunning()
		if err != nil {
			return err
		}
		if !running {
			return nil
		}
		if time.Now().After(deadline) {
			return errors.New("timed out waiting for local HDMI preview to release capture")
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func platformPreviewRunning() (bool, error) {
	entries, err := os.ReadDir("/proc")
	if err != nil {
		return false, err
	}
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		pid, parseErr := strconv.Atoi(entry.Name())
		if parseErr != nil {
			continue
		}
		comm, readErr := os.ReadFile("/proc/" + entry.Name() + "/comm")
		if readErr == nil && stringTrimSpace(comm) == "hdmi_preview" &&
			processAlive(pid) {
			return true, nil
		}
	}
	return false, nil
}

func platformReserveHDMIForAgent() error {
	platformVideoMu.Lock()
	defer platformVideoMu.Unlock()
	if platformVideoUsers != 0 || platformOwnsHDMI {
		return errors.New("pause the live IPKVM session before starting computer control")
	}
	platformAgentHDMI = true
	return nil
}

func platformReleaseHDMIForAgent() {
	platformVideoMu.Lock()
	platformAgentHDMI = false
	platformVideoMu.Unlock()
}

func platformReleaseHDMI() {
	if os.Getenv("AITVBOX_SHARED_CAPTURE") == "1" {
		return
	}
	if !platformOwnsHDMI {
		return
	}
	_ = os.Remove(envOrDefault("AITVBOX_HDMI_OWNER_FILE", defaultHDMIOwnerPath))
	platformOwnsHDMI = false
}

func processAlive(pid int) bool {
	if pid <= 1 {
		return false
	}
	stat, err := os.ReadFile(fmt.Sprintf("/proc/%d/stat", pid))
	if err == nil {
		nameEnd := bytes.LastIndexByte(stat, ')')
		if nameEnd >= 0 && len(stat) > nameEnd+2 &&
			stat[nameEnd+1] == ' ' && stat[nameEnd+2] == 'Z' {
			return false
		}
	}
	process, err := os.FindProcess(pid)
	return err == nil && process.Signal(syscall.Signal(0)) == nil
}

func dirName(path string) string {
	if index := lastSlash(path); index > 0 {
		return path[:index]
	}
	return "."
}

func lastSlash(value string) int {
	for index := len(value) - 1; index >= 0; index-- {
		if value[index] == '/' {
			return index
		}
	}
	return -1
}

func stringTrimSpace(value []byte) string {
	start, end := 0, len(value)
	for start < end && (value[start] == ' ' || value[start] == '\n' || value[start] == '\r' || value[start] == '\t') {
		start++
	}
	for end > start && (value[end-1] == ' ' || value[end-1] == '\n' || value[end-1] == '\r' || value[end-1] == '\t') {
		end--
	}
	return string(value[start:end])
}
