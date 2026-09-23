package main

import (
	"kvm"
	"os"
)

func main() {
	if os.Getenv("AITVBOX_PLATFORM_MODE") == "1" {
		kvm.MainPlatform()
		return
	}
	kvm.Main()
}
