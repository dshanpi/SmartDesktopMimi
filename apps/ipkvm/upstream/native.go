package kvm

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"time"

	"github.com/pion/webrtc/v4/pkg/media"
)

const (
	defaultVideoSampleDuration = time.Second / 60
	maxVideoTimestampGap       = 250 * time.Millisecond
	videoFrameMagic            = "AIV1"
	defaultH264ParameterCache  = "/var/run/aitvbox/h264-parameter-sets.bin"
	maxH264ParameterSetSize    = 4096
)

// videoTimestampClock derives RTP durations from timestamps captured before
// hardware encoding. Encoder callbacks and Unix socket writes may arrive in
// bursts; those transport timings must not alter the HDMI playback timeline.
type videoTimestampClock struct {
	lastMicros uint64
}

func (clock *videoTimestampClock) Next(timestampMicros uint64) time.Duration {
	if timestampMicros == 0 || clock.lastMicros == 0 ||
		timestampMicros <= clock.lastMicros {
		clock.lastMicros = timestampMicros
		return defaultVideoSampleDuration
	}
	gap := time.Duration(timestampMicros-clock.lastMicros) * time.Microsecond
	clock.lastMicros = timestampMicros
	if gap <= 0 || gap > maxVideoTimestampGap {
		return defaultVideoSampleDuration
	}
	return gap
}

func readVideoFrameHeader(reader io.Reader) (uint32, uint64, error) {
	var prefix [4]byte
	if _, err := io.ReadFull(reader, prefix[:]); err != nil {
		return 0, 0, err
	}
	if string(prefix[:]) != videoFrameMagic {
		return binary.LittleEndian.Uint32(prefix[:]), 0, nil
	}
	var versioned [12]byte
	if _, err := io.ReadFull(reader, versioned[:]); err != nil {
		return 0, 0, err
	}
	return binary.LittleEndian.Uint32(versioned[:4]),
		binary.LittleEndian.Uint64(versioned[4:]), nil
}

var (
	ctrlConnectionMu sync.RWMutex
	ctrlSocketConn   net.Conn
	ctrlWriteMu      sync.Mutex
)

type CtrlAction struct {
	Action string                 `json:"action"`
	Seq    int32                  `json:"seq,omitempty"`
	Params map[string]interface{} `json:"params,omitempty"`
}

type CtrlResponse struct {
	Seq    int32                  `json:"seq,omitempty"`
	Error  string                 `json:"error,omitempty"`
	Errno  int32                  `json:"errno,omitempty"`
	Result map[string]interface{} `json:"result,omitempty"`
	Event  string                 `json:"event,omitempty"`
	Data   json.RawMessage        `json:"data,omitempty"`
}

type EventHandler func(event CtrlResponse)

var (
	requestMu       sync.Mutex
	seq             int32 = 1
	ongoingRequests       = make(map[int32]chan *CtrlResponse)
)

var (
	videoCmd     *exec.Cmd
	videoCmdLock = &sync.Mutex{}
)

func CallCtrlAction(action string, params map[string]interface{}) (*CtrlResponse, error) {
	requestMu.Lock()
	requestSeq := seq
	seq++
	if seq <= 0 {
		seq = 1
	}
	responseChan := make(chan *CtrlResponse, 1)
	ongoingRequests[requestSeq] = responseChan
	requestMu.Unlock()
	removePending := func() {
		requestMu.Lock()
		if ongoingRequests[requestSeq] == responseChan {
			delete(ongoingRequests, requestSeq)
		}
		requestMu.Unlock()
	}

	ctrlAction := CtrlAction{
		Action: action,
		Seq:    requestSeq,
		Params: params,
	}

	jsonData, err := json.Marshal(ctrlAction)
	if err != nil {
		removePending()
		return nil, fmt.Errorf("error marshaling ctrl action: %w", err)
	}

	scopedLogger := videoLogger.With().
		Str("action", ctrlAction.Action).
		Interface("params", ctrlAction.Params).Logger()

	scopedLogger.Debug().Msg("sending ctrl action")

	err = WriteCtrlMessage(jsonData)
	if err != nil {
		removePending()
		return nil, ErrorfL(&scopedLogger, "error writing ctrl message", err)
	}

	select {
	case response := <-responseChan:
		removePending()
		if response.Error != "" {
			return nil, ErrorfL(
				&scopedLogger,
				"error native response: %s",
				errors.New(response.Error),
			)
		}
		return response, nil
	case <-time.After(5 * time.Second):
		removePending()
		return nil, ErrorfL(&scopedLogger, "timeout waiting for response", nil)
	}
}

func WriteCtrlMessage(message []byte) error {
	ctrlWriteMu.Lock()
	defer ctrlWriteMu.Unlock()
	ctrlConnectionMu.RLock()
	connection := ctrlSocketConn
	ctrlConnectionMu.RUnlock()
	if connection == nil {
		return fmt.Errorf("ctrl socket not connected")
	}
	for len(message) > 0 {
		written, err := connection.Write(message)
		if err != nil {
			return err
		}
		if written == 0 {
			return io.ErrShortWrite
		}
		message = message[written:]
	}
	return nil
}

var videoCtrlSocketListener net.Listener //nolint:unused
var videoSocketListener net.Listener     //nolint:unused

// Keyframe cache for new subscribers
// Cache SPS, PPS, and IDR separately to ensure proper decoding order
var cachedParameterSets []byte
var keyframeLock sync.Mutex

func h264ParameterCachePath() string {
	path := os.Getenv("AITVBOX_H264_PARAMETER_CACHE")
	if path == "" {
		return defaultH264ParameterCache
	}
	return path
}

func h264NALPresence(data []byte) (hasSPS, hasPPS, hasIDR bool) {
	for index := 0; index+3 < len(data); index++ {
		prefixLength := 0
		if index+4 < len(data) && data[index] == 0 && data[index+1] == 0 &&
			data[index+2] == 0 && data[index+3] == 1 {
			prefixLength = 4
		} else if data[index] == 0 && data[index+1] == 0 && data[index+2] == 1 {
			prefixLength = 3
		}
		if prefixLength == 0 || index+prefixLength >= len(data) {
			continue
		}
		switch data[index+prefixLength] & 0x1f {
		case 5:
			hasIDR = true
		case 7:
			hasSPS = true
		case 8:
			hasPPS = true
		}
		index += prefixLength
	}
	return
}

func setCachedH264ParameterSets(data []byte, persist bool) error {
	if len(data) == 0 || len(data) > maxH264ParameterSetSize {
		return fmt.Errorf("invalid H.264 parameter set size: %d", len(data))
	}
	hasSPS, hasPPS, _ := h264NALPresence(data)
	if !hasSPS || !hasPPS {
		return errors.New("H.264 parameter cache must contain SPS and PPS")
	}
	copyOfData := append([]byte(nil), data...)
	keyframeLock.Lock()
	cachedParameterSets = copyOfData
	keyframeLock.Unlock()
	if !persist {
		return nil
	}
	cachePath := h264ParameterCachePath()
	if err := os.MkdirAll(filepath.Dir(cachePath), 0755); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(cachePath), ".h264-params-*")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer os.Remove(temporaryPath)
	if err = temporary.Chmod(0644); err == nil {
		_, err = temporary.Write(copyOfData)
	}
	if err == nil {
		err = temporary.Sync()
	}
	if closeErr := temporary.Close(); err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	return os.Rename(temporaryPath, cachePath)
}

func loadCachedH264ParameterSets() error {
	data, err := os.ReadFile(h264ParameterCachePath())
	if err != nil {
		return err
	}
	return setCachedH264ParameterSets(data, false)
}

var ctrlClientConnected = make(chan struct{})

func waitCtrlClientConnected() {
	<-ctrlClientConnected
}

// ownedUnixListener removes only the socket node it created. This matters
// during a service restart: an old process may finish shutting down after the
// replacement process has already bound the same path. Blindly unlinking the
// path from the old process would make the new listener unreachable.
type ownedUnixListener struct {
	*net.UnixListener
	path      string
	boundInfo os.FileInfo
	closeOnce sync.Once
	closeErr  error
}

func (listener *ownedUnixListener) Close() error {
	listener.closeOnce.Do(func() {
		listener.closeErr = listener.UnixListener.Close()
		currentInfo, statErr := os.Lstat(listener.path)
		if statErr == nil && os.SameFile(listener.boundInfo, currentInfo) {
			if removeErr := os.Remove(listener.path); listener.closeErr == nil {
				listener.closeErr = removeErr
			}
		}
	})
	return listener.closeErr
}

func listenOwnedUnix(socketPath string) (net.Listener, error) {
	address, err := net.ResolveUnixAddr("unix", socketPath)
	if err != nil {
		return nil, err
	}
	unixListener, err := net.ListenUnix("unix", address)
	if err != nil {
		return nil, err
	}
	// The wrapper below performs inode-checked cleanup.
	unixListener.SetUnlinkOnClose(false)
	if err := os.Chmod(socketPath, 0600); err != nil {
		_ = unixListener.Close()
		_ = os.Remove(socketPath)
		return nil, err
	}
	boundInfo, err := os.Lstat(socketPath)
	if err != nil {
		_ = unixListener.Close()
		_ = os.Remove(socketPath)
		return nil, err
	}
	return &ownedUnixListener{
		UnixListener: unixListener,
		path:         socketPath,
		boundInfo:    boundInfo,
	}, nil
}

func StartVideoSocketServer(socketPath string, handleClient func(net.Conn), isCtrl bool) net.Listener {
	scopedLogger := videoLogger.With().
		Str("socket_path", socketPath).
		Logger()

	// Remove the socket file if it already exists
	if _, err := os.Stat(socketPath); err == nil {
		if err := os.Remove(socketPath); err != nil {
			scopedLogger.Warn().Err(err).Msg("failed to remove existing socket file")
			return nil
		}
	}

	listener, err := listenOwnedUnix(socketPath)
	if err != nil {
		scopedLogger.Warn().Err(err).Msg("failed to start server")
		return nil
	}

	scopedLogger.Info().Msg("server listening")

	go func() {
		for {
			scopedLogger.Debug().Msg("waiting for client connection")
			conn, err := listener.Accept()

			if err != nil {
				if errors.Is(err, net.ErrClosed) {
					return
				}
				scopedLogger.Warn().Err(err).Msg("failed to accept socket")
				continue
			}
			scopedLogger.Info().Str("remote_addr", conn.RemoteAddr().String()).Msg("new client connection accepted")
			if isCtrl {
				// check if the channel is closed
				select {
				case <-ctrlClientConnected:
					scopedLogger.Debug().Msg("ctrl client reconnected")
				default:
					close(ctrlClientConnected)
					scopedLogger.Debug().Msg("first native ctrl socket client connected")
				}
			}

			go handleClient(conn)
		}
	}()

	return listener
}

func StartVideoCtrlSocketServer() error {
	videoCtrlSocketListener = StartVideoSocketServer(
		envOrDefault("AITVBOX_KVM_CTRL_SOCKET", "/var/run/kvm_ctrl.sock"),
		handleCtrlClient,
		true,
	)
	if videoCtrlSocketListener == nil {
		return errors.New("cannot start native video control socket")
	}
	videoLogger.Debug().Msg("native app ctrl sock started")
	return nil
}

func StartVideoDataSocketServer() error {
	if err := loadCachedH264ParameterSets(); err != nil && !errors.Is(err, os.ErrNotExist) {
		videoLogger.Warn().Err(err).Msg("failed to load H.264 parameter cache")
	}
	videoSocketListener = StartVideoSocketServer(
		envOrDefault("AITVBOX_KVM_VIDEO_SOCKET", "/tmp/kvm_video_stream.sock"),
		handleVideoClient,
		false,
	)
	if videoSocketListener == nil {
		return errors.New("cannot start native video data socket")
	}
	videoLogger.Debug().Msg("native app video socket server started")
	return nil
}

func handleCtrlClient(conn net.Conn) {
	defer conn.Close()

	scopedLogger := videoLogger.With().
		Str("addr", conn.RemoteAddr().String()).
		Str("type", "ctrl").
		Logger()

	scopedLogger.Info().Msg("native ctrl socket client connected")
	ctrlConnectionMu.Lock()
	previous := ctrlSocketConn
	ctrlSocketConn = conn
	ctrlConnectionMu.Unlock()
	if previous != nil && previous != conn {
		scopedLogger.Debug().Msg("closing existing native socket connection")
		_ = previous.Close()
	}
	defer func() {
		ctrlConnectionMu.Lock()
		if ctrlSocketConn == conn {
			ctrlSocketConn = nil
		}
		ctrlConnectionMu.Unlock()
	}()

	// Restore HDMI EDID if applicable
	ensureConfigLoaded()
	if edid := config.EdidString; edid != "" {
		go restoreHdmiEdid(edid)
	}

	decoder := json.NewDecoder(conn)
	for {
		var ctrlResp CtrlResponse
		if err := decoder.Decode(&ctrlResp); err != nil {
			scopedLogger.Warn().Err(err).Msg("error reading from ctrl sock")
			break
		}
		scopedLogger.Trace().Interface("data", ctrlResp).Msg("ctrl sock msg")

		if ctrlResp.Seq != 0 {
			requestMu.Lock()
			responseChan, ok := ongoingRequests[ctrlResp.Seq]
			if ok {
				delete(ongoingRequests, ctrlResp.Seq)
			}
			requestMu.Unlock()
			if ok {
				select {
				case responseChan <- &ctrlResp:
				default:
				}
			}
		}
		switch ctrlResp.Event {
		case "video_input_state":
			HandleVideoStateMessage(ctrlResp)
		}
	}

	scopedLogger.Debug().Msg("ctrl sock disconnected")
}

func handleVideoClient(conn net.Conn) {
	defer conn.Close()

	scopedLogger := videoLogger.With().
		Str("addr", conn.RemoteAddr().String()).
		Str("type", "video").
		Logger()

	videoLogger.Info().Msg("=== handleVideoClient: client connected ===")
	scopedLogger.Info().Msg("native video socket client connected")

	// Current frame format:
	// [4B "AIV1"][4B LE frame_size][8B LE capture timestamp us][H.264].
	// The legacy [4B LE frame_size][H.264] envelope remains readable so the
	// web and capture binaries can be updated without a synchronized outage.
	inboundPacket := make([]byte, maxFrameSize)
	lastFrame := time.Now()
	frameCount := 0
	sampleClock := videoTimestampClock{}

	for {
		frameSize, captureTimestampMicros, err := readVideoFrameHeader(conn)
		if err != nil {
			if err != io.EOF {
				scopedLogger.Warn().Err(err).Msg("error reading video frame header")
			}
			break
		}

		if frameSize == 0 || frameSize > maxFrameSize {
			scopedLogger.Error().Uint32("frameSize", frameSize).Uint32("maxFrameSize", maxFrameSize).
				Msg("received invalid frame size")
			break
		}

		// Read the actual frame data
		_, err = io.ReadFull(conn, inboundPacket[:frameSize])
		if err != nil {
			scopedLogger.Warn().Err(err).Msg("error reading frame data")
			break
		}

		now := time.Now()
		sinceLastFrame := now.Sub(lastFrame)
		lastFrame = now
		sampleDuration := sampleClock.Next(captureTimestampMicros)
		frameCount++

		// Analyze NAL units in this frame
		var nalUnits []string
		hasSPS, hasPPS, hasIDR := h264NALPresence(inboundPacket[:frameSize])

		analyzeNALUnits := func(data []byte) {
			// Look for Annex-B start code: 4-byte (0x00 0x00 0x00 0x01) or 3-byte (0x00 0x00 0x01)
			i := 0
			for i < len(data)-5 {
				// Check 4-byte start code
				if data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x01 {
					nalType := data[i+4] & 0x1F
					nalName := ""
					switch nalType {
					case 1:
						nalName = "NON-IDR"
					case 5:
						nalName = "IDR"
					case 7:
						nalName = "SPS"
					case 8:
						nalName = "PPS"
					case 9:
						nalName = "AUD"
					default:
						nalName = fmt.Sprintf("NAL%d", nalType)
					}
					nalUnits = append(nalUnits, nalName)
					i += 4
					continue
				}
				// Check 3-byte start code
				if data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x01 {
					nalType := data[i+3] & 0x1F
					nalName := ""
					switch nalType {
					case 1:
						nalName = "NON-IDR"
					case 5:
						nalName = "IDR"
					case 7:
						nalName = "SPS"
					case 8:
						nalName = "PPS"
					case 9:
						nalName = "AUD"
					default:
						nalName = fmt.Sprintf("NAL%d", nalType)
					}
					nalUnits = append(nalUnits, nalName)
					i += 3
					continue
				}
				i++
			}
		}

		analyzeNALUnits(inboundPacket[:frameSize])

		// The A133 encoder emits SPS and PPS together in one Annex-B access
		// unit. Cache that unit once. The previous implementation copied the
		// same combined unit into both cachedSPS and cachedPPS, then prepended
		// it twice to every IDR, which can make browser decoders visibly reset
		// at each keyframe.
		if (hasSPS || hasPPS) && !hasIDR {
			if err := setCachedH264ParameterSets(inboundPacket[:frameSize], true); err != nil {
				scopedLogger.Warn().Err(err).Msg("failed to cache H.264 parameter sets")
			}
			if frameCount <= 10 {
				scopedLogger.Info().Int("frame", frameCount).Int("size", int(frameSize)).
					Msg("cached H.264 parameter sets")
			}
		}

		// Log first few frames
		if frameCount <= 10 {
			scopedLogger.Info().Int("frame", frameCount).Int("size", int(frameSize)).
				Int64("duration_ms", sinceLastFrame.Milliseconds()).
				Int64("rtp_duration_ms", sampleDuration.Milliseconds()).
				Uint64("capture_timestamp_us", captureTimestampMicros).
				Str("nal_units", fmt.Sprintf("%v", nalUnits)).
				Bool("has_sps", hasSPS).Bool("has_pps", hasPPS).Bool("has_idr", hasIDR).
				Msg("received video frame")
		}

		// Determine data to send
		dataToSend := inboundPacket[:frameSize]

		// For WebRTC H.264, prepend cached SPS/PPS to IDR frames that don't have them
		if hasIDR && !hasSPS && !hasPPS {
			keyframeLock.Lock()
			if cachedParameterSets != nil {
				combinedSize := len(cachedParameterSets) + int(frameSize)
				combined := make([]byte, combinedSize)
				copy(combined, cachedParameterSets)
				copy(combined[len(cachedParameterSets):], inboundPacket[:frameSize])
				dataToSend = combined
				if frameCount <= 10 {
					scopedLogger.Info().Int("prepended_size", combinedSize-int(frameSize)).
						Int("total_size", combinedSize).
						Msg("prepended cached SPS/PPS to IDR frame")
				}
			}
			keyframeLock.Unlock()
		}

		// Broadcast to HTTP clients
		dataCopy := make([]byte, len(dataToSend))
		copy(dataCopy, dataToSend)
		videoBroadcaster.Broadcast(dataCopy)

		// Queue the WebRTC write independently from the capture socket reader.
		// Slow Wi-Fi packetization may drop stale network frames, but it must
		// never reduce the HDMI capture/encode cadence.
		session := loadCurrentSession()
		if session != nil && session.VideoTrack != nil {
			session.enqueueVideoSample(media.Sample{
				Data:     dataCopy,
				Duration: sampleDuration,
			}, hasIDR)
		} else {
			if frameCount <= 10 {
				scopedLogger.Warn().Bool("session_nil", session == nil).Msg("no WebRTC session, frame not sent")
			}
		}
	}
}

func startVideoBinaryWithLock(binaryPath string) (*exec.Cmd, error) {
	videoCmdLock.Lock()
	defer videoCmdLock.Unlock()

	cmd, err := startVideoBinary(binaryPath)
	if err != nil {
		return nil, err
	}
	videoCmd = cmd
	return cmd, nil
}

func restartVideoBinary(binaryPath string) error {
	time.Sleep(10 * time.Second)
	// restart the binary
	videoLogger.Info().Msg("restarting kvm_video binary")
	cmd, err := startVideoBinary(binaryPath)
	if err != nil {
		videoLogger.Warn().Err(err).Msg("failed to restart binary")
	}
	videoCmd = cmd
	return err
}

func superviseVideoBinary(binaryPath string) error {
	videoCmdLock.Lock()
	defer videoCmdLock.Unlock()

	if videoCmd == nil || videoCmd.Process == nil {
		return restartVideoBinary(binaryPath)
	}

	err := videoCmd.Wait()

	if err == nil {
		videoLogger.Info().Err(err).Msg("kvm_video binary exited with no error")
	} else if exiterr, ok := err.(*exec.ExitError); ok {
		videoLogger.Warn().Int("exit_code", exiterr.ExitCode()).Msg("kvm_video binary exited with error")
	} else {
		videoLogger.Warn().Err(err).Msg("kvm_video binary exited with unknown error")
	}

	return restartVideoBinary(binaryPath)
}

func ExtractAndRunVideoBin() error {
	binaryPath := config.VideoBinPath
	if binaryPath == "" {
		binaryPath = "/userdata/100ask_kvm/bin/kvm_video"
	}

	// Check if binary exists
	if _, err := os.Stat(binaryPath); os.IsNotExist(err) {
		videoLogger.Warn().Str("path", binaryPath).Msg("video binary not found, skipping video capture")
		return nil // Don't error out, just skip video
	}

	// Make the binary executable
	if err := os.Chmod(binaryPath, 0755); err != nil {
		return fmt.Errorf("failed to make binary executable: %w", err)
	}
	// Run the binary in the background
	cmd, err := startVideoBinaryWithLock(binaryPath)
	if err != nil {
		return fmt.Errorf("failed to start binary: %w", err)
	}

	// check if the binary is still running every 10 seconds
	go func() {
		for {
			select {
			case <-appCtx.Done():
				videoLogger.Info().Msg("stopping native binary supervisor")
				return
			default:
				err := superviseVideoBinary(binaryPath)
				if err != nil {
					videoLogger.Warn().Err(err).Msg("failed to supervise native binary")
					time.Sleep(1 * time.Second) // Add a short delay to prevent rapid successive calls
				}
			}
		}
	}()

	go func() {
		<-appCtx.Done()
		videoLogger.Info().Int("pid", cmd.Process.Pid).Msg("killing process")
		err := cmd.Process.Kill()
		if err != nil {
			videoLogger.Warn().Err(err).Msg("failed to kill process")
			return
		}
	}()

	videoLogger.Info().Int("pid", cmd.Process.Pid).Msg("kvm_video binary started")

	return nil
}

// Restore the HDMI EDID value from the config.
func restoreHdmiEdid(edid string) {
	videoLogger.Info().Str("edid", edid).Msg("Restoring HDMI EDID")
	_, err := CallCtrlAction("set_edid", map[string]interface{}{"edid": edid})
	if err != nil {
		videoLogger.Warn().Err(err).Msg("Failed to restore HDMI EDID")
	}
}
