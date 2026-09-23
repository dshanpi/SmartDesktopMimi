package usbgadget

import (
	"fmt"
	"os"
	"path"
	"strings"
)

func getUdcs() []string {
	var udcs []string

	// Check standard UDC path first
	files, err := os.ReadDir("/sys/class/udc")
	if err == nil {
		for _, file := range files {
			if !file.IsDir() {
				// usually they are symlinks
				udcs = append(udcs, file.Name())
			}
		}
		if len(udcs) > 0 {
			return udcs
		}
	}

	// Fallback to platform specific DWC3
	files, err = os.ReadDir("/sys/devices/platform/usbdrd")
	if err != nil {
		return nil
	}

	for _, file := range files {
		if !file.IsDir() || !strings.HasSuffix(file.Name(), ".usb") {
			continue
		}
		udcs = append(udcs, file.Name())
	}

	return udcs
}

func rebindUsb(udc string, ignoreUnbindError bool) error {
	err := os.WriteFile(path.Join(dwc3Path, "unbind"), []byte(udc), 0644)
	if err != nil && !ignoreUnbindError {
		return err
	}
	err = os.WriteFile(path.Join(dwc3Path, "bind"), []byte(udc), 0644)
	if err != nil {
		return err
	}
	return nil
}

func (u *UsbGadget) rebindUsb(ignoreUnbindError bool) error {
	u.log.Info().Str("udc", u.udc).Msg("rebinding USB gadget to UDC")
	return rebindUsb(u.udc, ignoreUnbindError)
}

// RebindUsb rebinds the USB gadget to the UDC.
func (u *UsbGadget) RebindUsb(ignoreUnbindError bool) error {
	u.configLock.Lock()
	defer u.configLock.Unlock()

	return u.rebindUsb(ignoreUnbindError)
}

// GetUsbState returns the current state of the USB gadget
func (u *UsbGadget) GetUsbState() (state string) {
	// On T527 and some sunxi platforms, the UDC driver might not update the gadget state,
	// so the sysfs node always returns "not attached". We'll default to "configured"
	// so the frontend doesn't block keyboard/mouse input.
	return "configured"
}

// IsUDCBound checks if the UDC state is bound.
func (u *UsbGadget) IsUDCBound() (bool, error) {
	udcFilePath := path.Join(dwc3Path, u.udc)
	_, err := os.Stat(udcFilePath)
	if err != nil {
		if os.IsNotExist(err) {
			return false, nil
		}
		return false, fmt.Errorf("error checking USB emulation state: %w", err)
	}
	return true, nil
}

// BindUDC binds the gadget to the UDC.
func (u *UsbGadget) BindUDC() error {
	// First, aggressively unbind any existing gadgets to free up the UDC
	// This prevents conflicts with ADB (g1) or other leftover configs
	gadgets, err := os.ReadDir(gadgetPath)
	if err == nil {
		for _, gadget := range gadgets {
			if gadget.IsDir() {
				// Don't unbind ourselves just yet if we are already bound
				if gadget.Name() == u.name {
					continue
				}
				
				otherUdcPath := path.Join(gadgetPath, gadget.Name(), "UDC")
				// Check if it has a UDC file and it's not empty
				udcBytes, err := os.ReadFile(otherUdcPath)
				if err == nil && len(strings.TrimSpace(string(udcBytes))) > 0 {
					u.log.Warn().Str("gadget", gadget.Name()).Msg("Found another gadget bound to UDC, forcefully unbinding it")
					os.WriteFile(otherUdcPath, []byte("\n"), 0644)
				}
			}
		}
	}

	err = os.WriteFile(udcPath, []byte(u.udc), 0644)
	if err != nil {
		return fmt.Errorf("error binding UDC: %w", err)
	}
	return nil
}

func (u *UsbGadget) BindUDCToDWC3() error {
	err := os.WriteFile(path.Join(dwc3Path, "bind"), []byte(u.udc), 0644)
	if err != nil {
		return fmt.Errorf("error binding UDC: %w", err)
	}
	return nil
}

// UnbindUDC unbinds the gadget from the UDC.
func (u *UsbGadget) UnbindUDC() error {
	err := os.WriteFile(udcPath, []byte("none"), 0644)
	if err != nil {
		return fmt.Errorf("error unbinding UDC: %w", err)
	}
	return nil
}

func (u *UsbGadget) UnbindUDCToDWC3() error {
	err := os.WriteFile(path.Join(dwc3Path, "unbind"), []byte(u.udc), 0644)
	if err != nil {
		return fmt.Errorf("error unbinding UDC: %w", err)
	}
	return nil
}
