package kvm

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"kvm/internal/hidrpc"
	"kvm/internal/logging"

	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
	"github.com/gin-gonic/gin"
	"github.com/pion/rtcp"
	"github.com/pion/webrtc/v4"
	"github.com/pion/webrtc/v4/pkg/media"
	"github.com/rs/zerolog"
)

// A 1080p60 IDR is roughly 100 KiB on the A133. Keep a small amount of
// headroom for that burst, but do not let a healthy-yet-slower network writer
// accumulate a visibly stale quarter-second of interactive desktop video.
// Six frames cap this application-side queue at about 100 ms; overflow still
// drops the stale GOP and resumes at a clean IDR instead of blocking capture.
const videoSampleQueueSize = 6

type queuedVideoSample struct {
	sample   media.Sample
	keyFrame bool
}

type Session struct {
	peerConnection *webrtc.PeerConnection
	VideoTrack     *webrtc.TrackLocalStaticSample
	AudioTrack     *webrtc.TrackLocalStaticRTP
	//AudioTrack               *webrtc.TrackLocalStaticSample
	ControlChannel           *webrtc.DataChannel
	RPCChannel               *webrtc.DataChannel
	HidChannel               *webrtc.DataChannel
	DiskChannel              *webrtc.DataChannel
	shouldUmountVirtualMedia bool
	hidRPCAvailable          bool
	hidQueueLock             sync.Mutex
	hidQueue                 []chan hidQueueMessage
	keypress                 *keypressState
	videoSampleQueue         chan queuedVideoSample
	videoWriterStop          chan struct{}
	videoWriterStopOnce      sync.Once
	videoDroppedSamples      atomic.Uint64
	videoQueueOverflows      atomic.Uint64
	videoNeedsKeyFrame       atomic.Bool
	lastKeyFrameRequestNanos atomic.Int64
}

const minimumKeyFrameRequestInterval = 500 * time.Millisecond

func (s *Session) requestVideoKeyFrame(scopedLogger *zerolog.Logger) {
	now := time.Now().UnixNano()
	for {
		previous := s.lastKeyFrameRequestNanos.Load()
		if previous != 0 && time.Duration(now-previous) < minimumKeyFrameRequestInterval {
			return
		}
		if s.lastKeyFrameRequestNanos.CompareAndSwap(previous, now) {
			break
		}
	}
	var err error
	if isPlatformMode() {
		err = platformRequestVideoKeyFrame()
	} else {
		err = writeCtrlAction("start_video")
	}
	if err != nil {
		scopedLogger.Warn().Err(err).Msg("failed to request H.264 keyframe")
	}
}

func rtcpRequestsKeyFrame(packets []rtcp.Packet) bool {
	for _, packet := range packets {
		switch packet.(type) {
		case *rtcp.PictureLossIndication, *rtcp.FullIntraRequest:
			return true
		}
	}
	return false
}

type hidQueueMessage struct {
	webrtc.DataChannelMessage
	channel string
}

func (s *Session) initQueues() {
	s.hidQueueLock.Lock()
	defer s.hidQueueLock.Unlock()

	s.hidQueue = make([]chan hidQueueMessage, 0)
	for i := 0; i < 4; i++ {
		q := make(chan hidQueueMessage, 256)
		s.hidQueue = append(s.hidQueue, q)
	}
}

func (s *Session) handleQueues(index int) {
	for msg := range s.hidQueue[index] {
		onHidMessage(msg, s)
	}
}

func (s *Session) startVideoWriter(scopedLogger *zerolog.Logger) {
	s.videoSampleQueue = make(chan queuedVideoSample, videoSampleQueueSize)
	s.videoWriterStop = make(chan struct{})
	// A newly attached browser has no H.264 reference state. Starting it on
	// an arbitrary P-frame produces a black/corrupt picture until the next
	// decoder recovery point.
	s.videoNeedsKeyFrame.Store(true)
	go func() {
		for {
			select {
			case queued := <-s.videoSampleQueue:
				if s.VideoTrack == nil {
					continue
				}
				if err := s.VideoTrack.WriteSample(queued.sample); err != nil {
					scopedLogger.Warn().Err(err).Msg("error writing queued video sample")
					s.videoNeedsKeyFrame.Store(true)
					for {
						select {
						case <-s.videoSampleQueue:
						default:
							goto recovered
						}
					}
				recovered:
				}
			case <-s.videoWriterStop:
				return
			}
		}
	}()
}

func (s *Session) stopVideoWriter() {
	s.videoWriterStopOnce.Do(func() {
		if s.videoWriterStop != nil {
			close(s.videoWriterStop)
		}
	})
}

func (s *Session) enqueueVideoSample(sample media.Sample, keyFrame bool) {
	if s.videoSampleQueue == nil {
		return
	}
	if keyFrame {
		// An IDR is the recovery boundary. Discard queued stale interframes so
		// a congested client can resume from the newest complete picture.
		for {
			select {
			case <-s.videoSampleQueue:
			default:
				goto enqueue
			}
		}
	} else if s.videoNeedsKeyFrame.Load() {
		// A missing interframe breaks the H.264 reference chain. Forwarding
		// later P-frames only produces corruption until the next IDR, so keep
		// capture independent and wait for a clean decoder recovery point.
		s.videoDroppedSamples.Add(1)
		return
	}

enqueue:
	select {
	case s.videoSampleQueue <- queuedVideoSample{
		sample: sample, keyFrame: keyFrame,
	}:
		if keyFrame {
			s.videoNeedsKeyFrame.Store(false)
		}
	default:
		// Network packetization must never back-pressure HDMI capture. Once
		// an interframe is lost, discard the stale GOP and resume only at IDR.
		dropped := s.videoDroppedSamples.Add(1)
		overflows := s.videoQueueOverflows.Add(1)
		s.videoNeedsKeyFrame.Store(true)
		webrtcLogger.Warn().
			Uint64("overflows", overflows).
			Uint64("dropped_samples", dropped).
			Int("queue_capacity", cap(s.videoSampleQueue)).
			Msg("WebRTC video queue overflow; waiting for next IDR")
		for {
			select {
			case <-s.videoSampleQueue:
			default:
				return
			}
		}
	}
}

func getOnHidMessageHandler(session *Session, scopedLogger *zerolog.Logger, channel string) func(msg webrtc.DataChannelMessage) {
	return func(msg webrtc.DataChannelMessage) {
		l := scopedLogger.With().Str("channel", channel).Int("length", len(msg.Data)).Logger()

		if msg.IsString {
			l.Warn().Msg("received string data in HID RPC message handler")
			return
		}
		if len(msg.Data) < 1 {
			l.Warn().Msg("received empty data in HID RPC message handler")
			return
		}

		queueIndex := hidrpc.GetQueueIndex(hidrpc.MessageType(msg.Data[0]))
		if queueIndex >= len(session.hidQueue) || queueIndex < 0 {
			l.Warn().Int("queueIndex", queueIndex).Msg("queue index not found, using fallback")
			queueIndex = 3
		}

		queue := session.hidQueue[queueIndex]
		if queue == nil {
			l.Warn().Int("queueIndex", queueIndex).Msg("queue is nil")
			return
		}

		queue <- hidQueueMessage{
			DataChannelMessage: msg,
			channel:            channel,
		}
	}
}

type SessionConfig struct {
	ICEServers []string
	LocalIP    string
	ws         *websocket.Conn
	Logger     *zerolog.Logger
}

func (s *Session) ExchangeOffer(offerStr string) (string, error) {
	b, err := base64.StdEncoding.DecodeString(offerStr)
	if err != nil {
		return "", err
	}
	offer := webrtc.SessionDescription{}
	err = json.Unmarshal(b, &offer)
	if err != nil {
		return "", err
	}
	// Set the remote SessionDescription
	if err = s.peerConnection.SetRemoteDescription(offer); err != nil {
		return "", err
	}

	// Create answer
	answer, err := s.peerConnection.CreateAnswer(nil)
	if err != nil {
		return "", err
	}

	// Sets the LocalDescription, and starts our UDP listeners
	if err = s.peerConnection.SetLocalDescription(answer); err != nil {
		return "", err
	}

	localDescription, err := json.Marshal(s.peerConnection.LocalDescription())
	if err != nil {
		return "", err
	}

	// Add logging to see the SDP
	webrtcLogger.Info().Str("sdp", s.peerConnection.LocalDescription().SDP).Msg("Local SDP Answer")

	return base64.StdEncoding.EncodeToString(localDescription), nil
}

func newSession(sessionConfig SessionConfig) (*Session, error) {
	webrtcSettingEngine := webrtc.SettingEngine{
		LoggerFactory: logging.GetPionDefaultLoggerFactory(),
	}
	webrtcSettingEngine.SetNetworkTypes([]webrtc.NetworkType{
		webrtc.NetworkTypeUDP4,
		webrtc.NetworkTypeUDP6,
	})
	publicIP := ""
	if isPlatformMode() {
		mux, muxErr := refreshPlatformICEUDPMux()
		if muxErr != nil {
			return nil, muxErr
		}
		webrtcSettingEngine.SetICEUDPMux(mux)
		publicIP = strings.TrimSpace(os.Getenv("AITVBOX_WEBRTC_PUBLIC_IP"))
		if publicIP != "" {
			webrtcSettingEngine.SetNAT1To1IPs(
				[]string{publicIP},
				webrtc.ICECandidateTypeSrflx,
			)
		}
	}

	var scopedLogger *zerolog.Logger
	if sessionConfig.Logger != nil {
		l := sessionConfig.Logger.With().Str("component", "webrtc").Logger()
		scopedLogger = &l
	} else {
		scopedLogger = webrtcLogger
	}

	iceServers := []webrtc.ICEServer{}
	if publicIP == "" {
		iceServers = append(iceServers, webrtc.ICEServer{
			URLs: []string{"stun:stun.l.google.com:19302"},
		})
	}
	if config.STUN != "" && publicIP == "" {
		iceServers = append(iceServers, webrtc.ICEServer{
			URLs: []string{config.STUN},
		})
	}

	// pion/webrtc v4 has built-in codec support, no need for MediaEngine configuration
	api := webrtc.NewAPI(
		webrtc.WithSettingEngine(webrtcSettingEngine),
	)
	peerConnection, err := api.NewPeerConnection(webrtc.Configuration{
		ICEServers: iceServers,
	})
	if err != nil {
		return nil, err
	}
	session := &Session{peerConnection: peerConnection}
	session.initQueues()
	for i := 0; i < 4; i++ {
		go session.handleQueues(i)
	}

	peerConnection.OnDataChannel(func(d *webrtc.DataChannel) {
		scopedLogger.Info().Str("label", d.Label()).Uint16("id", *d.ID()).Msg("New DataChannel")
		switch d.Label() {
		case "rpc":
			session.RPCChannel = d
			d.OnMessage(func(msg webrtc.DataChannelMessage) {
				go onRPCMessage(msg, session)
			})
			triggerOTAStateUpdate()
			triggerVideoStateUpdate()
			triggerUSBStateUpdate()
		case "hidrpc":
			session.HidChannel = d
			d.OnMessage(getOnHidMessageHandler(session, scopedLogger, d.Label()))
		case "hidrpc-unreliable-ordered":
			d.OnMessage(getOnHidMessageHandler(session, scopedLogger, d.Label()))
		case "hidrpc-unreliable-nonordered":
			d.OnMessage(getOnHidMessageHandler(session, scopedLogger, d.Label()))
		case "disk":
			if isPlatformMode() {
				_ = d.Close()
			} else {
				session.DiskChannel = d
				d.OnMessage(onDiskMessage)
			}
		case "terminal":
			if isPlatformMode() {
				_ = d.Close()
			} else {
				handleTerminalChannel(d)
			}
		case "serial":
			if isPlatformMode() {
				_ = d.Close()
			} else {
				handleSerialChannel(d)
			}
		default:
			if strings.HasPrefix(d.Label(), uploadIdPrefix) {
				go handleUploadChannel(d)
			}
		}
	})

	if streamEncodecType == "hevc" {
		session.VideoTrack, err = webrtc.NewTrackLocalStaticSample(webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeH265}, "video", "kvm")
	} else {
		// This must match the CedarX encoder's SPS exactly. A profile mismatch
		// can make Chromium accept and count frames while rendering green.
		session.VideoTrack, err = webrtc.NewTrackLocalStaticSample(webrtc.RTPCodecCapability{
			MimeType:    webrtc.MimeTypeH264,
			SDPFmtpLine: "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f",
		}, "video", "kvm")
	}
	if err != nil {
		return nil, err
	}
	session.startVideoWriter(scopedLogger)

	session.AudioTrack, err = webrtc.NewTrackLocalStaticRTP(webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeOpus}, "audio", "kvm")
	if err != nil {
		return nil, err
	}

	rtpSender, err := peerConnection.AddTrack(session.VideoTrack)
	if err != nil {
		return nil, err
	}

	audioRtpSender, err := peerConnection.AddTrack(session.AudioTrack)
	if err != nil {
		return nil, err
	}

	// Read incoming RTCP packets
	// Before these packets are returned they are processed by interceptors. For things
	// like NACK this needs to be called.
	go func() {
		rtcpBuf := make([]byte, 1500)
		for {
			n, _, rtcpErr := rtpSender.Read(rtcpBuf)
			if rtcpErr != nil {
				return
			}
			packets, unmarshalErr := rtcp.Unmarshal(rtcpBuf[:n])
			if unmarshalErr == nil && rtcpRequestsKeyFrame(packets) {
				session.videoNeedsKeyFrame.Store(true)
				session.requestVideoKeyFrame(scopedLogger)
			}
		}
	}()

	go func() {
		audioRtcpBuf := make([]byte, 1500)
		for {
			if _, _, rtcpErr := audioRtpSender.Read(audioRtcpBuf); rtcpErr != nil {
				return
			}
		}
	}()

	var isConnected atomic.Bool

	peerConnection.OnICECandidate(func(candidate *webrtc.ICECandidate) {
		scopedLogger.Info().Interface("candidate", candidate).Msg("WebRTC peerConnection has a new ICE candidate")
		if candidate != nil {
			err := wsjson.Write(context.Background(), sessionConfig.ws, gin.H{"type": "new-ice-candidate", "data": candidate.ToJSON()})
			if err != nil {
				scopedLogger.Warn().Err(err).Msg("failed to write new-ice-candidate to WebRTC signaling channel")
			}
		}
	})

	peerConnection.OnICEConnectionStateChange(func(connectionState webrtc.ICEConnectionState) {
		scopedLogger.Info().Str("connectionState", connectionState.String()).Msg("ICE Connection State has changed")

		// WebRTC 连接成功
		if connectionState == webrtc.ICEConnectionStateConnected {
			if isConnected.CompareAndSwap(false, true) {
				count := actionSessions.Add(1)
				webrtcLogger.Info().Msg("★★★ WebRTC Session Connected ★★★")
				onActiveSessionsChanged()
				if count == 1 {
					onFirstSessionConnected()
				} else {
					session.requestVideoKeyFrame(scopedLogger)
				}
				if !isPlatformMode() {
					setNpuAppStatus()
				}
			}
		}

		// WebRTC 连接失败
		if connectionState == webrtc.ICEConnectionStateFailed {
			webrtcLogger.Error().Msg("!!! WebRTC Connection Failed !!!")
		}
		//state changes on closing browser tab disconnected->failed, we need to manually close it
		if connectionState == webrtc.ICEConnectionStateFailed {
			scopedLogger.Debug().Msg("ICE Connection State is failed, closing peerConnection")
			_ = peerConnection.Close()
		}
		if connectionState == webrtc.ICEConnectionStateClosed {
			scopedLogger.Debug().Msg("ICE Connection State is closed, unmounting virtual media")
			session.stopVideoWriter()
			clearCurrentSession(session)
			if session.shouldUmountVirtualMedia {
				err := rpcUnmountImage()
				scopedLogger.Warn().Err(err).Msg("unmount image failed on connection close")
			}
			if isConnected.CompareAndSwap(true, false) {
				count := actionSessions.Add(-1)
				onActiveSessionsChanged()
				if count == 0 {
					onLastSessionDisconnected()
				}
			}
		}
	})
	return session, nil
}

func onActiveSessionsChanged() {
	requestDisplayUpdate(true)
}

func onFirstSessionConnected() {
	if isPlatformMode() {
		platformStartVideo()
	} else {
		_ = writeCtrlAction("start_video")
	}
	// TODO: 调试阶段注释音频功能
	// if config.AudioMode != "disabled" {
	// 	StartNtpAudioServer(handleAudioClient)
	// }
}

func onLastSessionDisconnected() {
	if isPlatformMode() {
		platformStopVideo()
	} else {
		_ = writeCtrlAction("stop_video")
	}
	// TODO: 调试阶段注释音频功能
	// StopNtpAudioServer()
}
