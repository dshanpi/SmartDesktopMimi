package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"net/http/cookiejar"
	"net/url"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"
	"github.com/pion/rtp/codecs"
	"github.com/pion/webrtc/v4"
	"github.com/pion/webrtc/v4/pkg/media/samplebuilder"
)

type signalMessage struct {
	Type string          `json:"type"`
	Data json.RawMessage `json:"data"`
}

func fail(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "board-webrtc-smoke: "+format+"\n", args...)
	os.Exit(1)
}

func countH264RTPNALs(payload []byte, sps, pps, idr *atomic.Uint64) {
	if len(payload) == 0 {
		return
	}
	countType := func(nalType byte) {
		switch nalType {
		case 5:
			idr.Add(1)
		case 7:
			sps.Add(1)
		case 8:
			pps.Add(1)
		}
	}
	switch nalType := payload[0] & 0x1f; nalType {
	case 24: // STAP-A
		for offset := 1; offset+2 <= len(payload); {
			size := int(payload[offset])<<8 | int(payload[offset+1])
			offset += 2
			if size == 0 || offset+size > len(payload) {
				return
			}
			countType(payload[offset] & 0x1f)
			offset += size
		}
	case 28: // FU-A; count the original NAL once, on its start fragment.
		if len(payload) >= 2 && payload[1]&0x80 != 0 {
			countType(payload[1] & 0x1f)
		}
	default:
		countType(nalType)
	}
}

func asciiHID(character rune) (modifier, keycode byte, ok bool) {
	if character >= 'a' && character <= 'z' {
		return 0, byte(4 + character - 'a'), true
	}
	if character >= 'A' && character <= 'Z' {
		return 0x02, byte(4 + character - 'A'), true
	}
	if character >= '1' && character <= '9' {
		return 0, byte(30 + character - '1'), true
	}
	if character == '0' {
		return 0, 39, true
	}
	unshifted := map[rune]byte{
		' ': 44, '-': 45, '=': 46, '[': 47, ']': 48, '\\': 49,
		';': 51, '\'': 52, '`': 53, ',': 54, '.': 55, '/': 56,
	}
	if keycode, found := unshifted[character]; found {
		return 0, keycode, true
	}
	shifted := map[rune]byte{
		'!': 30, '@': 31, '#': 32, '$': 33, '%': 34, '^': 35,
		'&': 36, '*': 37, '(': 38, ')': 39, '_': 45, '+': 46,
		'{': 47, '}': 48, '|': 49, ':': 51, '"': 52, '~': 53,
		'<': 54, '>': 55, '?': 56,
	}
	if keycode, found := shifted[character]; found {
		return 0x02, keycode, true
	}
	return 0, 0, false
}

func main() {
	baseText := flag.String("url", "", "device base URL, for example http://192.168.1.44")
	password := flag.String("password", "", "configured local password")
	passwordFile := flag.String("password-file", "", "file containing the local password")
	authConfig := flag.String("auth-config", "", "config JSON containing an existing local auth token")
	timeout := flag.Duration("timeout", 20*time.Second, "overall test timeout")
	minPackets := flag.Uint64("min-packets", 5, "minimum video RTP packets")
	dumpH264 := flag.String("dump-h264", "", "optional Annex-B H.264 output path")
	minFrames := flag.Uint64("min-frames", 4, "frames to collect when dumping H.264")
	hidTest := flag.Bool("hid-test", false, "open hidrpc and send a relative mouse self-test")
	hidDelay := flag.Duration("hid-delay", time.Second, "delay before sending the HID self-test")
	hidClick := flag.Bool("hid-click", false, "click after moving to the bottom-right corner")
	hidClickButton := flag.Int("hid-click-button", 1, "mouse button mask used by -hid-click (1..7)")
	hidAbsoluteX := flag.Int("hid-absolute-x", -1, "absolute HID X coordinate (0..32767)")
	hidAbsoluteY := flag.Int("hid-absolute-y", -1, "absolute HID Y coordinate (0..32767)")
	hidRelativeX := flag.Int("hid-relative-x", 127, "relative HID X delta (-127..127)")
	hidRelativeY := flag.Int("hid-relative-y", 127, "relative HID Y delta (-127..127)")
	hidRelativeCount := flag.Int("hid-relative-count", 40, "number of relative HID reports")
	hidRelative2X := flag.Int("hid-relative2-x", 0, "second relative HID X delta (-127..127)")
	hidRelative2Y := flag.Int("hid-relative2-y", 0, "second relative HID Y delta (-127..127)")
	hidRelative2Count := flag.Int("hid-relative2-count", 0, "number of second relative HID reports")
	hidRelative3X := flag.Int("hid-relative3-x", 0, "third relative HID X delta (-127..127)")
	hidRelative3Y := flag.Int("hid-relative3-y", 0, "third relative HID Y delta (-127..127)")
	hidRelative3Count := flag.Int("hid-relative3-count", 0, "number of third relative HID reports")
	hidKeycode := flag.Int("hid-keycode", -1, "optional HID keyboard usage code (0..101)")
	hidKeyModifier := flag.Int("hid-key-modifier", 0, "optional HID keyboard modifier (0..255)")
	hidKeyHold := flag.Duration("hid-key-hold", 100*time.Millisecond, "keyboard press duration")
	hidRunCommand := flag.String("hid-run-command", "", "open Win+R and type this ASCII command")
	hidRunMoveRight := flag.Bool("hid-run-move-right", false, "move the Run dialog to the next monitor before typing")
	hidCharacterHold := flag.Duration("hid-character-hold", 50*time.Millisecond, "press duration for command characters")
	hidCommandWait := flag.Duration("hid-command-wait", 3*time.Second, "wait after executing -hid-run-command")
	postHIDDelay := flag.Duration("post-hid-delay", time.Second, "video capture delay after HID reports")
	flag.Parse()
	if *passwordFile != "" {
		passwordBytes, readErr := os.ReadFile(*passwordFile)
		if readErr != nil {
			fail("read password file: %v", readErr)
		}
		*password = strings.TrimSpace(string(passwordBytes))
	}
	if *baseText == "" || (*password == "" && *authConfig == "") {
		flag.Usage()
		os.Exit(2)
	}
	base, err := url.Parse(strings.TrimRight(*baseText, "/"))
	if err != nil || (base.Scheme != "http" && base.Scheme != "https") || base.Host == "" {
		fail("invalid URL")
	}

	jar, err := cookiejar.New(nil)
	if err != nil {
		fail("create cookie jar: %v", err)
	}
	client := &http.Client{Jar: jar, Timeout: 5 * time.Second}
	if *authConfig != "" {
		configBytes, readErr := os.ReadFile(*authConfig)
		if readErr != nil {
			fail("read auth config: %v", readErr)
		}
		var auth struct {
			LocalAuthToken string `json:"local_auth_token"`
		}
		if json.Unmarshal(configBytes, &auth) != nil || auth.LocalAuthToken == "" {
			fail("auth config does not contain a session token")
		}
		jar.SetCookies(base, []*http.Cookie{{Name: "authToken", Value: auth.LocalAuthToken}})
	} else {
		loginBody, _ := json.Marshal(map[string]string{"password": *password})
		response, loginErr := client.Post(
			base.String()+"/auth/login-local",
			"application/json",
			bytes.NewReader(loginBody),
		)
		if loginErr != nil {
			fail("login: %v", loginErr)
		}
		loginResponse, _ := io.ReadAll(io.LimitReader(response.Body, 4096))
		response.Body.Close()
		if response.StatusCode != http.StatusOK {
			fail("login status %s: %s", response.Status, strings.TrimSpace(string(loginResponse)))
		}
	}

	wsURL := *base
	if wsURL.Scheme == "https" {
		wsURL.Scheme = "wss"
	} else {
		wsURL.Scheme = "ws"
	}
	wsURL.Path = "/webrtc/signaling/client"
	headers := http.Header{"Origin": []string{base.String()}}
	for _, cookie := range jar.Cookies(base) {
		headers.Add("Cookie", cookie.String())
	}
	ctx, cancel := context.WithTimeout(context.Background(), *timeout)
	defer cancel()
	ws, _, err := websocket.Dial(ctx, wsURL.String(), &websocket.DialOptions{
		HTTPHeader: headers,
	})
	if err != nil {
		fail("websocket dial: %v", err)
	}
	defer ws.Close(websocket.StatusNormalClosure, "")

	peer, err := webrtc.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		fail("create peer connection: %v", err)
	}
	defer peer.Close()
	if _, err = peer.AddTransceiverFromKind(
		webrtc.RTPCodecTypeVideo,
		webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionRecvonly},
	); err != nil {
		fail("add video transceiver: %v", err)
	}
	if _, err = peer.CreateDataChannel("rpc", nil); err != nil {
		fail("create RPC data channel: %v", err)
	}
	hidReady := make(chan struct{})
	var hidReadyOnce sync.Once
	if *hidTest {
		hidChannel, createErr := peer.CreateDataChannel("hidrpc", nil)
		if createErr != nil {
			fail("create HID RPC data channel: %v", createErr)
		}
		hidChannel.OnOpen(func() {
			// Handshake: message type 1, protocol version 1.
			if sendErr := hidChannel.Send([]byte{0x01, 0x01}); sendErr != nil {
				fail("send HID handshake: %v", sendErr)
			}
		})
		hidChannel.OnMessage(func(message webrtc.DataChannelMessage) {
			if len(message.Data) < 2 || message.Data[0] != 0x01 ||
				message.Data[1] != 0x01 {
				return
			}
			go func() {
				select {
				case <-time.After(*hidDelay):
				case <-ctx.Done():
					return
				}
				sendKeyboard := func(modifier, keycode byte, hold time.Duration) {
					press := []byte{0x02, modifier, keycode, 0, 0, 0, 0, 0}
					if sendErr := hidChannel.Send(press); sendErr != nil {
						fail("send HID keyboard press: %v", sendErr)
					}
					time.Sleep(hold)
					if sendErr := hidChannel.Send([]byte{
						0x02, 0, 0, 0, 0, 0, 0, 0,
					}); sendErr != nil {
						fail("send HID keyboard release: %v", sendErr)
					}
					time.Sleep(15 * time.Millisecond)
				}
				if *hidKeycode >= 0 {
					if *hidKeycode > 101 || *hidKeyModifier < 0 || *hidKeyModifier > 255 {
						fail("keyboard code or modifier is out of range")
					}
					sendKeyboard(byte(*hidKeyModifier), byte(*hidKeycode), *hidKeyHold)
				}
				if *hidRunCommand != "" {
					// Open the Windows Run dialog with Left-GUI+R.
					sendKeyboard(0x08, 21, 100*time.Millisecond)
					time.Sleep(350 * time.Millisecond)
					if *hidRunMoveRight {
						// Left-GUI+Left-Shift+Right moves the active window to
						// the monitor on its right.
						sendKeyboard(0x0a, 79, 150*time.Millisecond)
						time.Sleep(500 * time.Millisecond)
					}
					for _, character := range *hidRunCommand {
						modifier, keycode, found := asciiHID(character)
						if !found {
							fail("unsupported command character %q", character)
						}
						sendKeyboard(modifier, keycode, *hidCharacterHold)
					}
					sendKeyboard(0, 40, 100*time.Millisecond) // Enter
					time.Sleep(*hidCommandWait)
				}
				if *hidAbsoluteX >= 0 || *hidAbsoluteY >= 0 {
					if *hidAbsoluteX < 0 || *hidAbsoluteX > 32767 ||
						*hidAbsoluteY < 0 || *hidAbsoluteY > 32767 {
						fail("absolute HID coordinates must both be in 0..32767")
					}
					pointerReport := make([]byte, 10)
					pointerReport[0] = 0x03
					binary.BigEndian.PutUint32(pointerReport[1:5], uint32(*hidAbsoluteX))
					binary.BigEndian.PutUint32(pointerReport[5:9], uint32(*hidAbsoluteY))
					if sendErr := hidChannel.Send(pointerReport); sendErr != nil {
						fail("send absolute HID mouse movement: %v", sendErr)
					}
					if *hidClick {
						pointerReport[9] = 1
						if sendErr := hidChannel.Send(pointerReport); sendErr != nil {
							fail("send absolute HID mouse press: %v", sendErr)
						}
						time.Sleep(50 * time.Millisecond)
						pointerReport[9] = 0
						if sendErr := hidChannel.Send(pointerReport); sendErr != nil {
							fail("send absolute HID mouse release: %v", sendErr)
						}
					}
				} else {
					// MouseReport is [type, dx, dy, buttons]. Repetition can
					// deliberately reach a host screen edge before a click.
					sendRelative := func(dx, dy, count int) {
						if dx < -127 || dx > 127 || dy < -127 || dy > 127 ||
							count < 0 {
							fail("relative HID movement is out of range")
						}
						for range count {
							if sendErr := hidChannel.Send([]byte{
								0x06,
								byte(int8(dx)),
								byte(int8(dy)),
								0x00,
							}); sendErr != nil {
								fail("send HID mouse movement: %v", sendErr)
							}
							time.Sleep(2 * time.Millisecond)
						}
					}
					sendRelative(*hidRelativeX, *hidRelativeY, *hidRelativeCount)
					sendRelative(*hidRelative2X, *hidRelative2Y, *hidRelative2Count)
					sendRelative(*hidRelative3X, *hidRelative3Y, *hidRelative3Count)
					if *hidClick {
						if *hidClickButton < 1 || *hidClickButton > 7 {
							fail("mouse click button mask is out of range")
						}
						if sendErr := hidChannel.Send([]byte{
							0x06, 0x00, 0x00, byte(*hidClickButton),
						}); sendErr != nil {
							fail("send HID mouse press: %v", sendErr)
						}
						time.Sleep(50 * time.Millisecond)
						if sendErr := hidChannel.Send([]byte{0x06, 0x00, 0x00, 0x00}); sendErr != nil {
							fail("send HID mouse release: %v", sendErr)
						}
					}
				}
				select {
				case <-time.After(*postHIDDelay):
				case <-ctx.Done():
					return
				}
				hidReadyOnce.Do(func() { close(hidReady) })
			}()
		})
	}

	var packets atomic.Uint64
	var payloadBytes atomic.Uint64
	var dumpedFrames atomic.Uint64
	var spsNALs atomic.Uint64
	var ppsNALs atomic.Uint64
	var idrNALs atomic.Uint64
	videoReady := make(chan struct{})
	var readyOnce sync.Once
	peer.OnTrack(func(track *webrtc.TrackRemote, _ *webrtc.RTPReceiver) {
		if track.Kind() != webrtc.RTPCodecTypeVideo {
			return
		}
		go func() {
			builder := samplebuilder.New(
				512,
				&codecs.H264Packet{},
				track.Codec().ClockRate,
			)
			var dump *os.File
			if *dumpH264 != "" {
				var createErr error
				dump, createErr = os.Create(*dumpH264)
				if createErr != nil {
					fail("create H.264 dump: %v", createErr)
				}
				defer dump.Close()
			}
			for {
				packet, _, readErr := track.ReadRTP()
				if readErr != nil {
					return
				}
				payloadBytes.Add(uint64(len(packet.Payload)))
				packetCount := packets.Add(1)
				countH264RTPNALs(packet.Payload, &spsNALs, &ppsNALs, &idrNALs)
				builder.Push(packet)
				for sample := builder.Pop(); sample != nil; sample = builder.Pop() {
					if dump != nil {
						if _, writeErr := dump.Write(sample.Data); writeErr != nil {
							fail("write H.264 dump: %v", writeErr)
						}
						frameCount := dumpedFrames.Add(1)
						if frameCount >= *minFrames {
							_ = dump.Sync()
							readyOnce.Do(func() { close(videoReady) })
						}
					}
				}
				if dump == nil && packetCount >= *minPackets {
					readyOnce.Do(func() { close(videoReady) })
				}
			}
		}()
	})

	var writeMu sync.Mutex
	writeSignal := func(kind string, data any) error {
		message, marshalErr := json.Marshal(map[string]any{"type": kind, "data": data})
		if marshalErr != nil {
			return marshalErr
		}
		writeMu.Lock()
		defer writeMu.Unlock()
		return ws.Write(ctx, websocket.MessageText, message)
	}
	peer.OnICECandidate(func(candidate *webrtc.ICECandidate) {
		if candidate != nil {
			_ = writeSignal("new-ice-candidate", candidate.ToJSON())
		}
	})

	offer, err := peer.CreateOffer(nil)
	if err != nil {
		fail("create offer: %v", err)
	}
	if err = peer.SetLocalDescription(offer); err != nil {
		fail("set local description: %v", err)
	}
	offerJSON, _ := json.Marshal(peer.LocalDescription())
	offerData := map[string]string{"sd": base64.StdEncoding.EncodeToString(offerJSON)}

	answerSet := make(chan struct{})
	go func() {
		defer cancel()
		offerSent := false
		for {
			_, raw, readErr := ws.Read(ctx)
			if readErr != nil {
				return
			}
			var message signalMessage
			if json.Unmarshal(raw, &message) != nil {
				continue
			}
			switch message.Type {
			case "device-metadata":
				if !offerSent {
					offerSent = true
					if writeSignal("offer", offerData) != nil {
						return
					}
				}
			case "answer":
				var encoded string
				if json.Unmarshal(message.Data, &encoded) != nil {
					return
				}
				answerJSON, decodeErr := base64.StdEncoding.DecodeString(encoded)
				if decodeErr != nil {
					return
				}
				var answer webrtc.SessionDescription
				if json.Unmarshal(answerJSON, &answer) != nil ||
					peer.SetRemoteDescription(answer) != nil {
					return
				}
				close(answerSet)
			case "new-ice-candidate":
				var candidate webrtc.ICECandidateInit
				if json.Unmarshal(message.Data, &candidate) == nil {
					_ = peer.AddICECandidate(candidate)
				}
			}
		}
	}()

	videoPassed := false
	hidPassed := !*hidTest
	for !videoPassed || !hidPassed {
		select {
		case <-videoReady:
			videoPassed = true
			videoReady = nil
		case <-hidReady:
			hidPassed = true
			hidReady = nil
		case <-ctx.Done():
			select {
			case <-answerSet:
				fail(
					"timed out after answer: video_rtp_packets=%d payload_bytes=%d sps=%d pps=%d idr=%d hid=%v",
					packets.Load(),
					payloadBytes.Load(),
					spsNALs.Load(),
					ppsNALs.Load(),
					idrNALs.Load(),
					hidPassed,
				)
			default:
				fail("timed out before WebRTC answer")
			}
		}
	}
	if videoPassed && hidPassed {
		fmt.Printf(
			"PASS video_rtp_packets=%d payload_bytes=%d dumped_frames=%d sps=%d pps=%d idr=%d hid=%v\n",
			packets.Load(),
			payloadBytes.Load(),
			dumpedFrames.Load(),
			spsNALs.Load(),
			ppsNALs.Load(),
			idrNALs.Load(),
			hidPassed,
		)
	}
}
