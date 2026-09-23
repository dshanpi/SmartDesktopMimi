package kvm

import (
	"context"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/Masterminds/semver/v3"
	"github.com/gwatts/rootcerts"
)

// Print build version at startup
var buildVersion = "dev"

func init() {
	buildVersion = builtAppVersion
}

var appCtx context.Context

func Main() {
	logger.Info().Str("version", buildVersion).Msg("=== KVM Application Starting ===")
	logger.Info().Msg("=== Main START ===")
	SyncConfigSD(true)
	logger.Info().Msg("=== SyncConfigSD done ===")
	LoadConfig()
	logger.Info().Msg("=== LoadConfig done ===")

	var cancel context.CancelFunc
	appCtx, cancel = context.WithCancel(context.Background())
	defer cancel()

	systemVersionLocal, appVersionLocal, err := GetLocalVersion()
	if err != nil {
		logger.Warn().Err(err).Msg("failed to get local version")
	}

	// TODO: 调试版本，设置为 0.0.0 启用所有功能
	minRequiredSystemVersion := semver.MustParse("0.0.0")
	isNewEnoughSystem := systemVersionLocal != nil && !systemVersionLocal.LessThan(minRequiredSystemVersion)

	logger.Info().
		Interface("system_version", systemVersionLocal).
		Interface("app_version", appVersionLocal).
		Msg("starting KVM")

	go runWatchdog()
	// TODO: 调试阶段注释 A/B 系统确认
	// go confirmCurrentSystem() //A/B system
	// TODO: 调试阶段注释热插拔检测
	// if isNewEnoughSystem {
	// 	go setForceHpd()
	// }

	http.DefaultClient.Timeout = 1 * time.Minute

	err = rootcerts.UpdateDefaultTransport()
	if err != nil {
		logger.Warn().Err(err).Msg("failed to load Root CA certificates")
	}
	logger.Info().
		Int("ca_certs_loaded", len(rootcerts.Certs())).
		Msg("loaded Root CA certificates")

	// Initialize network
	if err := initNetwork(); err != nil {
		logger.Error().Err(err).Msg("failed to initialize network")
		os.Exit(1)
	}

	// Initialize time sync
	initTimeSync()
	timeSync.Start()

	// Initialize mDNS
	if err := initMdns(); err != nil {
		logger.Error().Err(err).Msg("failed to initialize mDNS")
		os.Exit(1)
	}
	//if mDNS != nil {
	//	_ = mDNS.SetListenOptions(config.NetworkConfig.GetMDNSMode())
	//	_ = mDNS.SetLocalNames([]string{
	//		networkState.GetHostname(),
	//		networkState.GetFQDN(),
	//	}, true)
	//}

	// Initialize native ctrl socket server
	if err := StartVideoCtrlSocketServer(); err != nil {
		logger.Error().Err(err).Msg("failed to initialize native video control socket")
		os.Exit(1)
	}

	// Initialize native video socket server
	if err := StartVideoDataSocketServer(); err != nil {
		logger.Error().Err(err).Msg("failed to initialize native video data socket")
		os.Exit(1)
	}

	// Set up callbacks for HTTP video stream subscribers
	// When first HTTP subscriber connects and there's no WebRTC session, start video
	videoBroadcaster.onFirstSubscribe = func() {
		if activeSessionCount() == 0 {
			logger.Info().Msg("First HTTP video subscriber connected, starting video stream")
			_ = writeCtrlAction("start_video")
		}
	}
	// When last HTTP subscriber disconnects and there's no WebRTC session, stop video
	videoBroadcaster.onLastUnsubscribe = func() {
		if activeSessionCount() == 0 {
			logger.Info().Msg("Last HTTP video subscriber disconnected, stopping video stream")
			_ = writeCtrlAction("stop_video")
		}
	}

	// TODO: 调试阶段注释音频功能
	// StartAudioCtrlSocketServer()

	// TODO: 调试阶段注释 VPN 功能
	// StartVpnCtrlSocketServer()

	// TODO: 调试阶段注释显示控制功能
	// StartDisplayCtrlSocketServer()

	initPrometheus()

	go func() {
		err = ExtractAndRunVideoBin()
		if err != nil {
			logger.Warn().Err(err).Msg("failed to extract and run video bin")
			//TODO: prepare an error message screen buffer to show on kvm screen
		}

		// TODO: 调试阶段注释显示二进制
		// err = ExtractAndRunDisplayBin()
		// if err != nil {
		// 	logger.Warn().Err(err).Msg("failed to extract and run display bin")
		// }

		// TODO: 调试阶段注释音频二进制
		// err = ExtractAndRunAudioBin()
		// if err != nil {
		// 	logger.Warn().Err(err).Msg("failed to extract and run audio bin")
		// }

		// TODO: 调试阶段注释 VPN 二进制
		// err = ExtractAndRunVpnBin()
		// if err != nil {
		// 	logger.Warn().Err(err).Msg("failed to extract and run vpn bin")
		// }
	}()

	logger.Info().Msg("=== before initUsbGadget ===")
	// Initialize USB Gadget functionality
	if isNewEnoughSystem {
		initUsbGadget()
		logger.Info().Msg("=== after initUsbGadget ===")

		// 注释远程挂载镜像相关功能
		// if err := setInitialVirtualMediaState(); err != nil {
		// 	logger.Warn().Err(err).Msg("failed to set initial virtual media state")
		// }

		// if err := initImagesFolder(); err != nil {
		// 	logger.Warn().Err(err).Msg("failed to init images folder")
		// }

		// Initialize mouse jiggler (anti-sleep)
		initJiggler()

		// 注释系统信息功能 (虚拟U盘挂载)
		// initSystemInfo()
	}
	logger.Info().Msg("=== after isNewEnoughSystem block ===")

	// initialize GPIO (disabled for T527)
	// initGPIO()

	// initialize display (disabled - no display)
	// initDisplay()

	// Initialize VPN (disabled)
	// initVPN()

	//Auto update
	//go func() {
	//	time.Sleep(15 * time.Minute)
	//	for {
	//		logger.Debug().Bool("auto_update_enabled", config.AutoUpdateEnabled).Msg("UPDATING")
	//		if !config.AutoUpdateEnabled {
	//			return
	//		}
	//		if currentSession != nil {
	//			logger.Debug().Msg("skipping update since a session is active")
	//			time.Sleep(1 * time.Minute)
	//			continue
	//		}
	//		includePreRelease := config.IncludePreRelease
	//		err = TryUpdate(context.Background(), GetDeviceID(), includePreRelease)
	//		if err != nil {
	//			logger.Warn().Err(err).Msg("failed to auto update")
	//		}
	//		time.Sleep(1 * time.Hour)
	//	}
	//}()
	//go RunFuseServer()
	logger.Info().Msg("=== About to call RunWebServer ===")
	go RunWebServer()
	logger.Info().Msg("=== RunWebServer goroutine started ===")

	go RunWebSecureServer()
	// Web secure server is started only if TLS mode is enabled
	if config.TLSMode != "" {
		startWebSecureServer()
	}

	initSerialPort()
	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT, syscall.SIGTERM)
	<-sigs
	logger.Info().Msg("KVM Shutting Down")
	//if fuseServer != nil {
	//	err := setMassStorageImage(" ")
	//	if err != nil {
	//		logger.Infof("Failed to unmount mass storage image: %v", err)
	//	}
	//	err = fuseServer.Unmount()
	//	if err != nil {
	//		logger.Infof("Failed to unmount fuse: %v", err)
	//	}

	// os.Exit(0)
}
