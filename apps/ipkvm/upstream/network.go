package kvm

import (
	"fmt"
	"net"

	"kvm/internal/network"
	"kvm/internal/udhcpc"

	"github.com/guregu/null/v6"
)

const (
	// NetIfName - will be auto-detected
	NetIfName = ""
)

var (
	networkState *network.NetworkInterfaceState
	actualIfName string
)

// detectNetworkInterface finds the first connected (up with IP) network interface
func detectNetworkInterface() string {
	// Check all interfaces and return the first one that is up and has an IP
	ifaces, err := net.Interfaces()
	if err == nil {
		for _, iface := range ifaces {
			// Skip loopback, docker, and down interfaces
			if iface.Flags&net.FlagLoopback != 0 {
				continue
			}
			if iface.Flags&net.FlagUp == 0 {
				continue
			}
			if iface.Name == "docker0" || iface.Name == "br-" {
				continue
			}
			// Check if interface has an IP address
			addrs, err := iface.Addrs()
			if err == nil && len(addrs) > 0 {
				return iface.Name
			}
		}
	}

	// Fall back to wlan0 for WiFi systems
	if _, err := net.InterfaceByName("wlan0"); err == nil {
		return "wlan0"
	}

	// Fall back to eth0
	return "eth0"
}

func networkStateChanged() {
	// do not block the main thread
	go waitCtrlAndRequestDisplayUpdate(true)

	// always restart mDNS when the network state changes
	if mDNS != nil {
		_ = mDNS.SetListenOptions(config.NetworkConfig.GetMDNSMode())
		_ = mDNS.SetLocalNames([]string{
			networkState.GetHostname(),
			networkState.GetFQDN(),
		}, true)
	}
}

func initNetwork() error {
	ensureConfigLoaded()

	// Auto-detect network interface
	actualIfName = detectNetworkInterface()
	networkLogger.Info().Msgf("using network interface: %s", actualIfName)

	state, err := network.NewNetworkInterfaceState(&network.NetworkInterfaceOptions{
		DefaultHostname: GetDefaultHostname(),
		InterfaceName:   actualIfName,
		NetworkConfig:   config.NetworkConfig,
		Logger:          networkLogger,
		OnStateChange: func(state *network.NetworkInterfaceState) {
			networkStateChanged()
		},
		OnInitialCheck: func(state *network.NetworkInterfaceState) {
			networkStateChanged()
		},
		OnDhcpLeaseChange: func(lease *udhcpc.Lease) {
			networkStateChanged()

			session := loadCurrentSession()
			if session == nil {
				return
			}

			writeJSONRPCEvent("networkState", networkState.RpcGetNetworkState(), session)
		},
		OnConfigChange: func(networkConfig *network.NetworkConfig) {
			config.NetworkConfig = networkConfig
			config.AppliedNetworkConfig = networkConfig
			networkStateChanged()
		},
	})

	if state == nil {
		if err == nil {
			return fmt.Errorf("failed to create NetworkInterfaceState")
		}
		return err
	}

	if err := state.Run(); err != nil {
		return err
	}

	networkState = state

	if config != nil && config.NetworkConfig != nil {
		if config.NetworkConfig.PendingReboot.Valid && config.NetworkConfig.PendingReboot.Bool {
			if config.AppliedNetworkConfig != nil && network.IsSame(config.AppliedNetworkConfig, *config.NetworkConfig) {
				config.NetworkConfig.PendingReboot = null.BoolFrom(false)
				_ = SaveConfig()
			}
		}
	}

	if config != nil && config.NetworkConfig != nil {
		if config.NetworkConfig.PendingReboot.Valid && config.NetworkConfig.PendingReboot.Bool {
			config.NetworkConfig.PendingReboot = null.BoolFrom(false)
			_ = SaveConfig()
		}
	}

	return nil
}

func rpcGetNetworkState() network.RpcNetworkState {
	return networkState.RpcGetNetworkState()
}

func rpcGetNetworkSettings() network.RpcNetworkSettings {
	rpcSettings := networkState.RpcGetNetworkSettings()
	hostname := GetHostname()
	rpcSettings.Hostname = null.NewString(hostname, hostname != "")
	return rpcSettings
}

func rpcSetNetworkSettings(settings network.RpcNetworkSettings) (*network.RpcNetworkSettings, error) {
	current := networkState.RpcGetNetworkSettings()
	changedCore := !network.IsSame(current.NetworkConfig, settings.NetworkConfig)
	if changedCore {
		settings.NetworkConfig.PendingReboot = null.BoolFrom(true)
	}

	s := networkState.RpcSetNetworkSettings(settings)
	if s != nil {
		return nil, s
	}
	applied := networkState.RpcGetNetworkSettings()
	config.NetworkConfig = &applied.NetworkConfig
	// If we just reverted to the same core config as applied, clear pending_reboot
	if config.AppliedNetworkConfig != nil {
		// create copies ignoring PendingReboot
		a := *config.AppliedNetworkConfig
		b := applied.NetworkConfig
		a.PendingReboot = null.Bool{}
		b.PendingReboot = null.Bool{}
		if network.IsSame(a, b) {
			config.NetworkConfig.PendingReboot = null.BoolFrom(false)
		}
	}
	if err := SaveConfig(); err != nil {
		return nil, err
	}

	return &network.RpcNetworkSettings{NetworkConfig: *config.NetworkConfig}, nil
}

func rpcRenewDHCPLease() error {
	return networkState.RpcRenewDHCPLease()
}

func rpcRequestDHCPAddress(ip string) error {
	return networkState.RpcRequestDHCPAddress(ip)
}
