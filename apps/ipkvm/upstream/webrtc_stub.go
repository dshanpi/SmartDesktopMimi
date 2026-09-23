// Stub for webrtc disk functions

package kvm

import "github.com/pion/webrtc/v4"

const uploadIdPrefix = "upload-"

var diskReadChan chan []byte

func onDiskMessage(msg webrtc.DataChannelMessage) {
	// not supported
}

func handleUploadChannel(d *webrtc.DataChannel) {
	// not supported
}
