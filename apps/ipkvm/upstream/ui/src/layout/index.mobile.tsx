import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Outlet,
  useLoaderData,
  useLocation,
  useNavigate,
  useOutlet,
  useSearchParams,
} from "react-router-dom";
import { useInterval } from "usehooks-ts";
import { FocusTrap } from "focus-trap-react";
import useWebSocket from "react-use-websocket";
import { isDesktop, isMobile } from "react-device-detect";
import {  Modal as AntdModal } from "antd";
import {useReactAt} from 'i18n-auto-extractor/react'
import semver from "semver";

import {
  HidState,
  KeyboardLedState,
  NetworkState,
  UpdateState,
  useDeviceStore,
  useHidStore,
  useMountMediaStore,
  useNetworkStateStore,
  User,
  useRTCStore,
  useUiStore,
  useUpdateStore,
  useVideoStore,
  VideoState,
  useSettingsStore,
 useVpnStore } from "@/hooks/stores";
import { JsonRpcRequest, useJsonRpc, resetHttpSessionId } from "@/hooks/useJsonRpc";
import { doRpcHidHandshake } from "@/hooks/useHidRpc";
import Modal from "@components/Modal";
import { useDeviceUiNavigation } from "@/hooks/useAppNavigation";
import {
  ConnectionFailedOverlay,
  LoadingConnectionOverlay,
  PeerConnectionDisconnectedOverlay,
} from "@components/VideoOverlay";
import { FeatureFlagProvider } from "@/providers/FeatureFlagProvider";
import notifications from "@/notifications";
import BarTop from "@/layout/core/bar_top/index";
import BottomBar from "@/layout/core/bar_bottom/index";
import { LocalVersionInfo } from "@/layout/components_setting/version/VersionContent";
import Desktop from "@/layout/core/desktop/index";
import { FullscreenProvider } from "@/layout/core/desktop/contexts/FullscreenContext";
import { dark_bg_style_fun } from "@/layout/theme_color";
import SidebarContainer from "@/layout/core/bar_side";
import OtherSessionRoute from "@/layout/core/other-session";
import { useTheme } from "@/layout/contexts/ThemeContext";
import { redirectToLoginIfSessionExpired } from "@/authRecovery";


import enJSON from '../locales/en.json';
import zhJSON from '../locales/zh.json';

interface LocalLoaderResp {
  authMode: "password" | "noPassword" | null;
}

interface CloudLoaderResp {
  deviceName: string;
  user: User | null;
  iceConfig: {
    iceServers: { credential?: string; urls: string | string[]; username?: string };
  } | null;
}

export type AuthMode = "password" | "noPassword" | null;
export interface LocalDevice {
  authMode: AuthMode;
  deviceId: string;
}

interface TailScaleResponse {
  state: string;
  loginUrl: string;
  ip: string;
  xEdge: boolean;
}

interface ZeroTierResponse {
  state: string;
  networkID: string;
  ip: string;
}



export default function MobileHome() {
  const { $at } = useReactAt();
  const loaderResp = useLoaderData() as LocalLoaderResp | CloudLoaderResp;
  // Depending on the mode, we set the appropriate variables
  const iceConfig = "iceConfig" in loaderResp ? loaderResp.iceConfig : null;

  const sidebarView = useUiStore(state => state.sidebarView);
  const topBarView = useUiStore(state => state.topBarView);

  const setIsTurnServerInUse = useRTCStore(state => state.setTurnServerInUse);
  const peerConnection = useRTCStore(state => state.peerConnection);
  const peerConnectionRef = useRef<RTCPeerConnection | null>(peerConnection);
  const setPeerConnectionState = useRTCStore(state => state.setPeerConnectionState);
  const peerConnectionState = useRTCStore(state => state.peerConnectionState);

  const setMediaMediaStream = useRTCStore(state => state.setMediaStream);
  const setPeerConnection = useRTCStore(state => state.setPeerConnection);
  const setDiskChannel = useRTCStore(state => state.setDiskChannel);
  const setRpcDataChannel = useRTCStore(state => state.setRpcDataChannel);
  const setRpcHidChannel = useRTCStore(state => state.setRpcHidChannel);
  const setRpcHidUnreliableChannel = useRTCStore(state => state.setRpcHidUnreliableChannel);
  const setRpcHidUnreliableNonOrderedChannel = useRTCStore(state => state.setRpcHidUnreliableNonOrderedChannel);
  const setRpcHidProtocolVersion = useRTCStore(state => state.setRpcHidProtocolVersion);
  const setTransceiver = useRTCStore(state => state.setTransceiver);
  const setAudioTransceiver = useRTCStore(state => state.setAudioTransceiver);
  const location = useLocation();

  const isLegacySignalingEnabled = useRef(false);

  const [connectionFailed, setConnectionFailed] = useState(false);

  const forceHttp = useSettingsStore(state => state.forceHttp);
  const { setOtaState } = useUpdateStore();

  const [loadingMessage, setLoadingMessage] = useState("Connecting to device...");
  const cleanupAndStopReconnecting = useCallback(
    function cleanupAndStopReconnecting(expectedPeerConnection?: RTCPeerConnection) {
      const activePeerConnection = peerConnectionRef.current;
      if (
        expectedPeerConnection &&
        activePeerConnection !== expectedPeerConnection
      ) {
        console.log("Ignoring cleanup from a stale peer connection");
        return;
      }

      console.log("Closing peer connection");
      setConnectionFailed(true);
      if (activePeerConnection) {
        setPeerConnectionState(activePeerConnection.connectionState);
      }
      connectionFailedRef.current = true;

      activePeerConnection?.close();
      peerConnectionRef.current = null;
      signalingAttempts.current = 0;
    },
    [setPeerConnectionState],
  );

  // We need to track connectionFailed in a ref to avoid stale closure issues
  // This is necessary because syncRemoteSessionDescription is a callback that captures
  // the connectionFailed value at creation time, but we need the latest value
  // when the function is actually called. Without this ref, the function would use
  // a stale value of connectionFailed in some conditions.
  //
  // We still need the state variable for UI rendering, so we sync the ref with the state.
  // This pattern is a workaround for what useEvent hook would solve more elegantly
  // (which would give us a callback that always has access to latest state without re-creation).
  const connectionFailedRef = useRef(false);
  useEffect(() => {
    connectionFailedRef.current = connectionFailed;
  }, [connectionFailed]);

  const signalingAttempts = useRef(0);
  const setRemoteSessionDescription = useCallback(
    async function setRemoteSessionDescription(
      pc: RTCPeerConnection,
      remoteDescription: RTCSessionDescriptionInit,
    ) {
      if (useSettingsStore.getState().forceHttp) {
        console.log("[setRemoteSessionDescription] Skipping due to HTTP fallback/force mode");
        return;
      }

      setLoadingMessage("Setting remote description");

      try {
        await pc.setRemoteDescription(new RTCSessionDescription(remoteDescription));
        console.log("[setRemoteSessionDescription] Remote description set successfully");
        setLoadingMessage("Establishing secure connection...");
      } catch (error) {
        console.error(
          "[setRemoteSessionDescription] Failed to set remote description:",
          error,
        );
        cleanupAndStopReconnecting(pc);
        return;
      }

      // Replace the interval-based check with a more reliable approach
      let attempts = 0;
      const checkInterval = setInterval(() => {
        if (peerConnectionRef.current !== pc) {
          clearInterval(checkInterval);
          return;
        }
        attempts++;

        // When vivaldi has disabled "Broadcast IP for Best WebRTC Performance", this never connects
        if (pc.sctp?.state === "connected") {
          console.log("[setRemoteSessionDescription] Remote description set");
          clearInterval(checkInterval);
          setLoadingMessage("Connection established");
        } else if (attempts >= 10) {
          console.log(
            "[setRemoteSessionDescription] Failed to establish connection after 10 attempts",
            {
              connectionState: pc.connectionState,
              iceConnectionState: pc.iceConnectionState,
            },
          );
          cleanupAndStopReconnecting(pc);
          clearInterval(checkInterval);
        } else {
          console.log("[setRemoteSessionDescription] Waiting for connection, state:", {
            connectionState: pc.connectionState,
            iceConnectionState: pc.iceConnectionState,
          });
        }
      }, 1000);
    },
    [cleanupAndStopReconnecting],
  );

  const ignoreOffer = useRef(false);
  const isSettingRemoteAnswerPending = useRef(false);
  const makingOffer = useRef(false);
  const sessionSuperseded = useRef(false);

  const wsProtocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const pendingCandidates = useRef<RTCIceCandidateInit[]>([]);

  const { sendMessage, getWebSocket } = useWebSocket(
    //`${wsProtocol}//${window.location.host}/webrtc/signaling/client?id=${params.id}`,
    `${wsProtocol}//${window.location.host}/webrtc/signaling/client`,
    {
      heartbeat: true,
      retryOnError: true,
      // Device upgrades and service restarts can take longer than a few seconds.
      // Keep signaling alive so the existing page recovers without a manual reload.
      reconnectAttempts: Number.MAX_SAFE_INTEGER,
      reconnectInterval: 1000,
      onReconnectStop: () => {
        console.log("Reconnect stopped");
        cleanupAndStopReconnecting();
      },

      shouldReconnect(event) {
        console.log("[Websocket] shouldReconnect", event);
        return !sessionSuperseded.current;
      },

      onClose(event) {
        console.log("[Websocket] onClose", event);
        // Drop the stale peer before reconnecting. Legacy signaling closes the
        // websocket intentionally and must keep its peer alive.
        if (!isLegacySignalingEnabled.current) {
          cleanupAndStopReconnecting();
          void redirectToLoginIfSessionExpired();
        }
      },

      onError(event) {
        console.log("[Websocket] onError", event);
        // onClose owns cleanup; the hook owns retry scheduling.
      },
      onOpen() {
        console.log("[Websocket] onOpen");
        setConnectionFailed(false);
        connectionFailedRef.current = false;
      },

      onMessage: message => {
        if (message.data === "pong") return;

        /*
          Currently the signaling process is as follows:
            After open, the other side will send a `device-metadata` message with the device version
            If the device version is not set, we can assume the device is using the legacy signaling
            Otherwise, we can assume the device is using the new signaling

            If the device is using the legacy signaling, we close the websocket connection
            and use the legacy HTTPSignaling function to get the remote session description

            If the device is using the new signaling, we don't need to do anything special, but continue to use the websocket connection
            to chat with the other peer about the connection
        */

        const parsedMessage = JSON.parse(message.data);
        if (parsedMessage.type === "device-metadata") {
          const { deviceVersion } = parsedMessage.data;
          console.log("[Websocket] Received device-metadata message");
          console.log("[Websocket] Device version", deviceVersion);
          // If the device version is not set, we can assume the device is using the legacy signaling
          if (!deviceVersion) {
            console.log("[Websocket] Device is using legacy signaling");

            // Now we don't need the websocket connection anymore, as we've established that we need to use the legacy signaling
            // which does everything over HTTP(at least from the perspective of the client)
            isLegacySignalingEnabled.current = true;
            getWebSocket()?.close();
          } else {
            console.log("[Websocket] Device is using new signaling");
            isLegacySignalingEnabled.current = false;
          }
          setupPeerConnection();
        }

        const activePeerConnection = peerConnectionRef.current;
        if (!activePeerConnection) return;
        if (parsedMessage.type === "answer") {
          console.log("[Websocket] Received answer");
          const readyForOffer =
            // If we're making an offer, we don't want to accept an answer
            !makingOffer &&
            // If the peer connection is stable or we're SettingsModal the remote answer pending, we're ready for an offer
            (activePeerConnection.signalingState === "stable" ||
              isSettingRemoteAnswerPending.current);

          // If we're not ready for an offer, we don't want to accept an offer
          ignoreOffer.current = parsedMessage.type === "offer" && !readyForOffer;
          if (ignoreOffer.current) return;

          // Set so we don't accept an answer while we're SettingsModal the remote description
          isSettingRemoteAnswerPending.current = parsedMessage.type === "answer";
          console.log(
            "[Websocket] Setting remote answer pending",
            isSettingRemoteAnswerPending.current,
          );

          const sd = atob(parsedMessage.data);
          const remoteSessionDescription = JSON.parse(sd);

          setRemoteSessionDescription(
            activePeerConnection,
            new RTCSessionDescription(remoteSessionDescription),
          ).then(() => {
            while (pendingCandidates.current.length > 0) {
              const candidate = pendingCandidates.current.shift();
              if (candidate) {
                activePeerConnection.addIceCandidate(candidate).catch(error => {
                  console.error("[Websocket] Error adding pending ICE candidate", error);
                });
              }
            }
          });

          // Reset the remote answer pending flag
          isSettingRemoteAnswerPending.current = false;
        } else if (parsedMessage.type === "new-ice-candidate") {
          console.log("[Websocket] Received new-ice-candidate");
          const candidate = parsedMessage.data;
          if (activePeerConnection.remoteDescription) {
            activePeerConnection.addIceCandidate(candidate).catch(error => {
              console.error("[Websocket] Error adding ICE candidate", error);
            });
          } else {
            pendingCandidates.current.push(candidate);
          }
        }
      },
    },

    isLegacySignalingEnabled.current === false,
  );

  const sendWebRTCSignal = useCallback(
    (type: string, data: unknown) => {
      // Second argument tells the library not to queue the message, and send it once the connection is established again.
      // We have event handlers that handle the connection set up, so we don't need to queue the message.
      sendMessage(JSON.stringify({ type, data }), false);
    },
    [sendMessage],
  );

  const setupPeerConnection = useCallback(async () => {
    if (
      useSettingsStore.getState().forceHttp
    ) {
      console.log("[setupPeerConnection] Skipping due to HTTP fallback/force mode");
      return;
    }

    // Re-entering from the other-session prompt is an explicit takeover.
    // Until that happens, a displaced page must stay parked instead of
    // reconnecting and displacing the browser that just took control.
    const wasSuperseded = sessionSuperseded.current;
    sessionSuperseded.current = false;
    if (wasSuperseded && getWebSocket()?.readyState !== WebSocket.OPEN) {
      window.location.assign("/");
      return;
    }

    console.log("[setupPeerConnection] Setting up peer connection");
    setConnectionFailed(false);
    setLoadingMessage("Connecting to device...");

    let pc: RTCPeerConnection;
    try {
      console.log("[setupPeerConnection] Creating peer connection");
      setLoadingMessage("Creating peer connection...");
      pc = new RTCPeerConnection({
        // We only use STUN or TURN servers if we're in the cloud
        //...(isInCloud && iceConfig?.iceServers
        //  ? { iceServers: [iceConfig?.iceServers] }
        //  : {}),
        ...(iceConfig?.iceServers
          ? { iceServers: [iceConfig?.iceServers] }
          : {
            iceServers: [
              {
                urls: ['stun:stun.l.google.com:19302']
              }
            ]
          }),
      });
      peerConnectionRef.current = pc;

      setPeerConnectionState(pc.connectionState);
      console.log("[setupPeerConnection] Peer connection created", pc);
      setLoadingMessage("Setting up connection to device...");
    } catch (e) {
      console.error(`[setupPeerConnection] Error creating peer connection: ${e}`);
      setTimeout(() => {
        cleanupAndStopReconnecting();
      }, 1000);
      return;
    }

    // Set up event listeners and data channels
    pc.onconnectionstatechange = () => {
      if (peerConnectionRef.current !== pc) return;
      console.log("[setupPeerConnection] Connection state changed", pc.connectionState);
      setPeerConnectionState(pc.connectionState);
    };

    pc.onnegotiationneeded = async () => {
      if (peerConnectionRef.current !== pc) return;
      try {
        console.log("[setupPeerConnection] Creating offer");
        makingOffer.current = true;

        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        const sd = btoa(JSON.stringify(pc.localDescription));
        const isNewSignalingEnabled = isLegacySignalingEnabled.current === false;
        if (isNewSignalingEnabled) {
          sendWebRTCSignal("offer", { sd: sd });
        } else {
          console.log("Legacy signanling. Waiting for ICE Gathering to complete...");
        }
      } catch (e) {
        console.error(
          `[setupPeerConnection] Error creating offer: ${e}`,
          new Date().toISOString(),
        );
        cleanupAndStopReconnecting(pc);
      } finally {
        makingOffer.current = false;
      }
    };

    pc.onicecandidate = async ({ candidate }) => {
      if (peerConnectionRef.current !== pc) return;
      if (!candidate) return;
      if (candidate.candidate === "") return;
      sendWebRTCSignal("new-ice-candidate", candidate);
    };

    pc.onicegatheringstatechange = event => {
      const pc = event.currentTarget as RTCPeerConnection;
      if (pc.iceGatheringState === "complete") {
        console.log("ICE Gathering completed");
        setLoadingMessage("ICE Gathering completed");

      } else if (pc.iceGatheringState === "gathering") {
        console.log("ICE Gathering Started");
        setLoadingMessage("Gathering ICE candidates...");
      }
    };

    pc.ontrack = function (event) {
      console.log('[RTP] ✅ ontrack 事件触发 - 收到 RTP 流');
      console.log('[RTP] track.kind:', event.track.kind);
      console.log('[RTP] track.label:', event.track.label);
      console.log('[RTP] stream.id:', event.streams[0]?.id);
      console.log('[RTP] stream.videoTracks:', event.streams[0]?.getVideoTracks().length);
      console.log('[RTP] stream.audioTracks:', event.streams[0]?.getAudioTracks().length);

      if (event.track.kind === 'video') {
        event.track.enabled = true;
        const lowLatencyReceiver = event.receiver as RTCRtpReceiver & { playoutDelayHint?: number };
        if ('playoutDelayHint' in lowLatencyReceiver) {
          lowLatencyReceiver.playoutDelayHint = 0;
        }
        console.log('[RTP] ✅ 视频轨道已启用');
      }
      if (event.track.kind === 'audio') {
        event.track.enabled = true;
        console.log('[RTP] ✅ 音频轨道已启用');
      }

      setMediaMediaStream(event.streams[0]);
    };

    setTransceiver(pc.addTransceiver("video", { direction: "recvonly" }));
    pc.addTransceiver("audio", { direction: "recvonly" });

    const rpcDataChannel = pc.createDataChannel("rpc");
    rpcDataChannel.onopen = () => {
      setRpcDataChannel(rpcDataChannel);
    };

    const rpcHidChannel = pc.createDataChannel("hidrpc");
    rpcHidChannel.binaryType = "arraybuffer";
    rpcHidChannel.onopen = () => {
      setRpcHidChannel(rpcHidChannel);
      doRpcHidHandshake(rpcHidChannel, setRpcHidProtocolVersion);
    };

    const rpcHidUnreliableChannel = pc.createDataChannel("hidrpc-unreliable-ordered", {
      ordered: true,
      maxRetransmits: 0,
    });
    rpcHidUnreliableChannel.binaryType = "arraybuffer";
    rpcHidUnreliableChannel.onopen = () => setRpcHidUnreliableChannel(rpcHidUnreliableChannel);

    const rpcHidUnreliableNonOrderedChannel = pc.createDataChannel("hidrpc-unreliable-nonordered", {
      ordered: false,
      maxRetransmits: 0,
    });
    rpcHidUnreliableNonOrderedChannel.binaryType = "arraybuffer";
    rpcHidUnreliableNonOrderedChannel.onopen = () =>
      setRpcHidUnreliableNonOrderedChannel(rpcHidUnreliableNonOrderedChannel);

    const diskDataChannel = pc.createDataChannel("disk");
    diskDataChannel.onopen = () => {
      setDiskChannel(diskDataChannel);
    };

    setPeerConnection(pc);
  }, [
    forceHttp,
    cleanupAndStopReconnecting,
    getWebSocket,
    iceConfig?.iceServers,
    sendWebRTCSignal,
    setDiskChannel,
    setMediaMediaStream,
    setPeerConnection,
    setPeerConnectionState,
    setRpcDataChannel,
    setRpcHidChannel,
    setRpcHidUnreliableChannel,
    setRpcHidUnreliableNonOrderedChannel,
    setRpcHidProtocolVersion,
    setTransceiver,
    setAudioTransceiver,
  ]);

  useEffect(() => {
    if (peerConnectionState === "failed") {
      if (sessionSuperseded.current) {
        console.log("Connection superseded; waiting for explicit takeover");
        cleanupAndStopReconnecting();
        return;
      }
      console.log("Connection failed, restarting signaling");
      cleanupAndStopReconnecting();
      // Reopening signaling delivers fresh device metadata and creates a new
      // peer connection. The websocket hook keeps retrying across maintenance.
      getWebSocket()?.close();
    }
  }, [peerConnectionState, cleanupAndStopReconnecting, getWebSocket]);

  // Cleanup effect
  const setSidebarView = useUiStore(state => state.setSidebarView);

  useEffect(() => {
    return () => {
      peerConnectionRef.current?.close();
      peerConnectionRef.current = null;
    };
  }, []);

  // For some reason, we have to have this unmount separate from the cleanup effect above
  useEffect(() => {
    return () => {
      setSidebarView(null);
      setPeerConnection(null);
    };
  }, [setPeerConnection, setSidebarView]);

  // TURN server usage detection
  useEffect(() => {
    // TURN detection is disabled after removing WebRTC stats
  }, [peerConnectionState, setIsTurnServerInUse]);

  // Vpn State Update
  const tailScaleConnectionState = useVpnStore(state => state.tailScaleConnectionState);
  const setTailScaleConnectionState = useVpnStore(state => state.setTailScaleConnectionState);
  const setTailScaleXEdge = useVpnStore(state => state.setTailScaleXEdge);
  const setTailScaleLoginUrl = useVpnStore(state => state.setTailScaleLoginUrl);
  const setTailScaleIP = useVpnStore(state => state.setTailScaleIP);
  const zeroTierConnectionState = useVpnStore(state => state.zeroTierConnectionState);

  const setZeroTierConnectionState = useVpnStore(state => state.setZeroTierConnectionState);
  const setZeroTierNetworkID = useVpnStore(state => state.setZeroTierNetworkID);
  const setZeroTierIP = useVpnStore(state => state.setZeroTierIP);
  const otherSession = useUiStore(state => state.otherSession);
  const setOtherSession = useUiStore(state => state.setOtherSession);
  const updateVpnStates = () => {
    // TailScaleState
    if (tailScaleConnectionState !== "connecting" && tailScaleConnectionState !== "closed") {
      send("getTailScaleSettings", {}, resp => {
        if ("error" in resp) return;
        const result = resp.result as TailScaleResponse;
        const validState = ["closed", "connecting", "connected", "disconnected", "logined"].includes(result.state)
          ? result.state as "closed" | "connecting" | "connected" | "disconnected" | "logined"
          : "closed";

        if(tailScaleConnectionState !== "disconnected" ) {
          setTailScaleXEdge(result.xEdge);
        }
        setTailScaleConnectionState(validState);
        setTailScaleLoginUrl(result.loginUrl);
        setTailScaleIP(result.ip);
      });
    }

    // ZeroTier
    if (zeroTierConnectionState !== "connecting" && zeroTierConnectionState !== "closed") {
      send("getZeroTierSettings", {}, resp => {
        if ("error" in resp) return;
        const result = resp.result as ZeroTierResponse;
        const validState = ["closed", "connecting", "connected", "disconnected", "logined"].includes(result.state)
          ? result.state as "closed" | "connecting" | "connected" | "disconnected" | "logined"
          : "closed";
        setZeroTierConnectionState(validState);
        setZeroTierNetworkID(result.networkID);
        setZeroTierIP(result.ip);
      });
    }
  }

  useInterval(updateVpnStates, 5000);

  const setNetworkState = useNetworkStateStore(state => state.setNetworkState);

  const setUsbState = useHidStore(state => state.setUsbState);
  const setHdmiState = useVideoStore(state => state.setHdmiState);

  const keyboardLedState = useHidStore(state => state.keyboardLedState);
  const setKeyboardLedState = useHidStore(state => state.setKeyboardLedState);

  const setKeyboardLedStateSyncAvailable = useHidStore(state => state.setKeyboardLedStateSyncAvailable);

  const [hasUpdated, setHasUpdated] = useState(false);
  const [sessionInvalidated, setSessionInvalidated] = useState(false);
  const { navigateTo } = useDeviceUiNavigation();

  function onJsonRpcRequest(resp: JsonRpcRequest) {
    if (resp.method === "otherSessionConnected") {
      sessionSuperseded.current = true;
      //navigateTo("/other-session");
      setOtherSession(true);
    }

    if (resp.method === "sessionInvalidated") {
      resetHttpSessionId();
      setSessionInvalidated(true);
      return;
    }

    if (resp.method === "usbState") {
      setUsbState(resp.params as unknown as HidState["usbState"]);
    }

    if (resp.method === "videoInputState") {
      setHdmiState(resp.params as Parameters<VideoState["setHdmiState"]>[0]);
    }

    if (resp.method === "networkState") {
      console.log("Setting network state", resp.params);
      setNetworkState(resp.params as NetworkState);
    }

    if (resp.method === "keyboardLedState") {
      const ledState = resp.params as KeyboardLedState;
      console.log("Setting keyboard led state", ledState);
      setKeyboardLedState(ledState);
      setKeyboardLedStateSyncAvailable(true);
    }

    if (resp.method === "otaState") {
      const otaState = resp.params as UpdateState["otaState"];
      setOtaState(otaState);

      if (otaState.updating === true) {
        setHasUpdated(true);
      }
    }
  }

  const rpcDataChannel = useRTCStore(state => state.rpcDataChannel);
  const [send] = useJsonRpc(onJsonRpcRequest);

  const updateUsbState = useCallback(() => {
    send("getUSBState", {}, resp => {
      if ("error" in resp) return;
      setUsbState(resp.result as HidState["usbState"]);
    });
  }, [send, setUsbState]);

  const updateVideoState = useCallback(() => {
    send("getVideoState", {}, resp => {
      if ("error" in resp) return;
      setHdmiState(resp.result as Parameters<VideoState["setHdmiState"]>[0]);
    });
  }, [send, setHdmiState]);

  useEffect(() => {
    if (rpcDataChannel?.readyState !== "open") return;
    updateVideoState();
    updateUsbState();
    updateVpnStates();
  }, [rpcDataChannel?.readyState, updateUsbState, updateVideoState]);

  useEffect(() => {
    if (!forceHttp) return;
    updateVideoState();
    updateUsbState();
  }, [forceHttp, updateUsbState, updateVideoState]);

  useInterval(() => {
    updateVideoState();
    updateUsbState();
  }, forceHttp ? 1000 : null);

  // request keyboard led state from the device
  useEffect(() => {
    if (rpcDataChannel?.readyState !== "open") return;
    if (keyboardLedState !== undefined) return;
    console.log("Requesting keyboard led state");

    send("getKeyboardLedState", {}, resp => {
      if ("error" in resp) {
        // -32601 means the method is not supported
        if (resp.error.code === -32601) {
          setKeyboardLedStateSyncAvailable(false);
          console.error("Failed to get keyboard led state, disabling sync", resp.error);
        } else {
          console.error("Failed to get keyboard led state", resp.error);
        }
        return;
      }
      console.log("Keyboard led state", resp.result);
      setKeyboardLedState(resp.result as KeyboardLedState);
      setKeyboardLedStateSyncAvailable(true);
    });
  }, [rpcDataChannel?.readyState, send, setKeyboardLedState, setKeyboardLedStateSyncAvailable, keyboardLedState]);

  const diskChannel = useRTCStore(state => state.diskChannel)!;
  const file = useMountMediaStore(state => state.localFile)!;
  useEffect(() => {
    if (!diskChannel || !file) return;
    diskChannel.onmessage = async e => {
      console.log("Received", e.data);
      const data = JSON.parse(e.data);
      const blob = file.slice(data.start, data.end);
      const buf = await blob.arrayBuffer();
      const header = new ArrayBuffer(16);
      const headerView = new DataView(header);
      headerView.setBigUint64(0, BigInt(data.start), false); // start offset, big-endian
      headerView.setBigUint64(8, BigInt(buf.byteLength), false); // length, big-endian
      const fullData = new Uint8Array(header.byteLength + buf.byteLength);
      fullData.set(new Uint8Array(header), 0);
      fullData.set(new Uint8Array(buf), header.byteLength);
      diskChannel.send(fullData);
    };
  }, [diskChannel, file]);

  // System update
  const disableKeyboardFocusTrap = useUiStore(state => state.disableVideoFocusTrap);

  // const [kvmTerminal, setKvmTerminal] = useState<RTCDataChannel | null>(null);
  // const [serialConsole, setSerialConsole] = useState<RTCDataChannel | null>(null);



  const outlet = useOutlet();
  const onModalClose = useCallback(() => {
    if (location.pathname !== "/other-session") navigateTo("/");
  }, [navigateTo, location.pathname]);

  const appVersion = useDeviceStore(state => state.appVersion);
  const hasConnectionFailed =
    connectionFailed || ["failed", "closed"].includes(peerConnectionState ?? "");
  const ConnectionStatusElement = useMemo(() => {


    const isPeerConnectionLoading =
      ["connecting", "new"].includes(peerConnectionState ?? "") ||
      peerConnection === null;

    const isDisconnected = peerConnectionState === "disconnected" && !forceHttp;

    const isOtherSession = location.pathname.includes("other-session");
    const hasActiveTopOrSidebar = topBarView !== null || sidebarView !== null;

    if (isOtherSession) return null;
    if (hasActiveTopOrSidebar) return null;
    if (peerConnectionState === "connected") return null;
    if (isDisconnected) {
      return <PeerConnectionDisconnectedOverlay show={true} />;
    }

    if (hasConnectionFailed)
      return (
        <ConnectionFailedOverlay show={true} setupPeerConnection={setupPeerConnection} />
      );
    if (forceHttp) return null;

    if (isPeerConnectionLoading) {
      return <LoadingConnectionOverlay show={true} text={loadingMessage} />;
    }

    return null;
  }, [
    connectionFailed,
    loadingMessage,
    location.pathname,
    peerConnection,
    peerConnectionState,
    setupPeerConnection,
    sidebarView,
    topBarView,
  ]);



const {isDark} = useTheme();

const language = useSettingsStore(state => state.language);
const { setCurrentLang } = useReactAt();
// Initialize Language
useEffect(() => {
  setCurrentLang(language, language === 'en' ? enJSON : zhJSON);
}, [language, setCurrentLang]);

  return (
    <FeatureFlagProvider appVersion={appVersion}>
      {sidebarView==null && topBarView == null && !hasConnectionFailed && isMobile}
      <div className="h-full overflow-hidden">

        {sessionInvalidated && (
          <div className="absolute inset-0 z-[20000] flex items-center justify-center bg-black/60">
            <div className="rounded-md bg-white px-6 py-4 text-center shadow-lg dark:bg-slate-800">
              <p className="mb-2 text-base font-semibold text-slate-900 dark:text-white">
                {$at("The current page has been launched")}
              </p>
              <p className="text-sm text-slate-600 dark:text-slate-300">
                {$at("Please close this page or continue using the device in a new page.")}
              </p>
            </div>
          </div>
        )}

        <FocusTrap
          paused={disableKeyboardFocusTrap}
          focusTrapOptions={{
            allowOutsideClick: true,
            escapeDeactivates: false,
            fallbackFocus: "#videoFocusTrap",
          }}
        >
          <div className="absolute top-0">
            <button className="absolute top-0  bg-fuchsia-300" tabIndex={-1} id="videoFocusTrap" />
          </div>
        </FocusTrap>

        <div className={`grid h-full grid-rows-(--grid-headerBody) select-none ${dark_bg_style_fun(isDark)}`}>

          <FullscreenProvider>
            <BarTop />


            <div className="relative flex h-full w-full overflow-hidden">
              <Desktop />
            <div
              style={{ animationDuration: "500ms" }}
              className={`animate-slideUpFade pointer-events-none absolute inset-0 flex items-center justify-center ${isMobile ?"":"p-4"}`}
            >
              <div className={`relative h-full  w-full ${isMobile ?"": "max-h-[720px] max-w-[1280px]"} rounded-md`}>
                {/*<ConnectionFailedOverlay show={true} setupPeerConnection={setupPeerConnection} />*/}
                {!!ConnectionStatusElement && ConnectionStatusElement}
              </div>
            </div>
            </div>

            {isDesktop&&<SidebarContainer sidebarView={sidebarView} />}
           {sidebarView !== "TerminalTabsMobile" && sidebarView !== "PowerControl" && <BottomBar />}
          </FullscreenProvider>
        </div>
      </div>

      <div
        className="z-50"
        onClick={e => e.stopPropagation()}
        onMouseUp={e => e.stopPropagation()}
        onMouseDown={e => e.stopPropagation()}
        onKeyUp={e => e.stopPropagation()}
        onKeyDown={e => {
          e.stopPropagation();
          if (e.key === "Escape") navigateTo("/");
        }}
      >
        <Modal open={outlet !== null} onClose={onModalClose}>
          {/* The 'used by other session' modal needs to have access to the connectWebRTC function */}
          <Outlet context={{ setupPeerConnection }} />
        </Modal>
        <AntdModal
          open={otherSession}
          modalRender={() => <OtherSessionRoute setupPeerConnection={setupPeerConnection} />}
        >

        </AntdModal>
      </div>

    </FeatureFlagProvider>
  );
}
