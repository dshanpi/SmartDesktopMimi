import { useCallback, useEffect, useMemo } from "react";

import { useRTCStore } from "@/hooks/stores";

import {
  CancelKeyboardMacroReportMessage,
  HID_RPC_VERSION,
  HandshakeMessage,
  KeyboardMacroStep,
  KeyboardMacroReportMessage,
  KeyboardReportMessage,
  KeypressKeepAliveMessage,
  KeypressReportMessage,
  MouseReportMessage,
  PointerReportMessage,
  RpcMessage,
  WheelReportMessage,
  unmarshalHidRpcMessage,
} from "./hidRpc";

const KEEPALIVE_MESSAGE = new KeypressKeepAliveMessage();

interface SendMessageParams {
  ignoreHandshakeState?: boolean;
  useUnreliableChannel?: boolean;
  requireOrdered?: boolean;
}

const HANDSHAKE_TIMEOUT = 30 * 1000;
const HANDSHAKE_MAX_ATTEMPTS = 10;

export function doRpcHidHandshake(
  rpcHidChannel: RTCDataChannel,
  setRpcHidProtocolVersion: (version: number | null) => void,
) {
  let attempts = 0;
  let lastConnectedTime: Date | undefined;
  let lastSendTime: Date | undefined;
  let handshakeCompleted = false;
  let handshakeInterval: ReturnType<typeof setInterval> | null = null;

  const shouldGiveUp = () => {
    if (attempts > HANDSHAKE_MAX_ATTEMPTS) {
      return true;
    }

    const timeSinceConnected = lastConnectedTime ? Date.now() - lastConnectedTime.getTime() : 0;
    return timeSinceConnected > HANDSHAKE_TIMEOUT;
  };

  const resetHandshake = ({
    lastConnectedTime: newLastConnectedTime,
    completed,
  }: {
    lastConnectedTime?: Date | undefined;
    completed?: boolean;
  }) => {
    if (newLastConnectedTime) lastConnectedTime = newLastConnectedTime;
    lastSendTime = undefined;
    attempts = 0;
    if (completed !== undefined) handshakeCompleted = completed;
    if (handshakeInterval) {
      clearInterval(handshakeInterval);
      handshakeInterval = null;
    }
  };

  const sendHandshake = (initial: boolean) => {
    if (handshakeCompleted) return;

    attempts++;
    lastSendTime = new Date();

    if (!initial && shouldGiveUp()) {
      if (handshakeInterval) {
        clearInterval(handshakeInterval);
        handshakeInterval = null;
      }
      return;
    }

    let data: Uint8Array | undefined;
    try {
      const message = new HandshakeMessage(HID_RPC_VERSION);
      data = message.marshal();
    } catch {
      return;
    }
    if (!data) return;
    rpcHidChannel.send(data as unknown as ArrayBuffer);

    if (initial) {
      handshakeInterval = setInterval(() => {
        sendHandshake(false);
      }, 1000);
    }
  };

  const onMessage = (ev: MessageEvent) => {
    const message = unmarshalHidRpcMessage(new Uint8Array(ev.data));
    if (!message || !(message instanceof HandshakeMessage)) return;

    if (!message.version) {
      return;
    }

    if (message.version > HID_RPC_VERSION) {
      return;
    }

    setRpcHidProtocolVersion(message.version);

    rpcHidChannel.removeEventListener("message", onMessage);
    resetHandshake({ completed: true });
    void lastSendTime;
  };

  const onConnected = () => {
    resetHandshake({ lastConnectedTime: new Date() });
    sendHandshake(true);
    rpcHidChannel.addEventListener("message", onMessage);
  };

  const onClose = () => {
    resetHandshake({ lastConnectedTime: undefined, completed: false });
    setRpcHidProtocolVersion(null);
    rpcHidChannel.removeEventListener("message", onMessage);
  };

  rpcHidChannel.addEventListener("open", onConnected);
  rpcHidChannel.addEventListener("close", onClose);

  if (rpcHidChannel.readyState === "open") {
    onConnected();
  }
}

export function useHidRpc(onHidRpcMessage?: (payload: RpcMessage) => void) {
  const {
    rpcHidChannel,
    rpcHidUnreliableChannel,
    rpcHidUnreliableNonOrderedChannel,
    setRpcHidProtocolVersion,
    rpcHidProtocolVersion,
  } = useRTCStore();

  const rpcHidReady = useMemo(() => {
    return rpcHidChannel?.readyState === "open" && rpcHidProtocolVersion !== null;
  }, [rpcHidChannel, rpcHidProtocolVersion]);

  const rpcHidUnreliableReady = useMemo(() => {
    return rpcHidUnreliableChannel?.readyState === "open" && rpcHidProtocolVersion !== null;
  }, [rpcHidProtocolVersion, rpcHidUnreliableChannel?.readyState]);

  const rpcHidUnreliableNonOrderedReady = useMemo(() => {
    return (
      rpcHidUnreliableNonOrderedChannel?.readyState === "open" && rpcHidProtocolVersion !== null
    );
  }, [rpcHidProtocolVersion, rpcHidUnreliableNonOrderedChannel?.readyState]);

  const rpcHidStatus = useMemo(() => {
    if (!rpcHidChannel) return "N/A";
    if (rpcHidChannel.readyState !== "open") return rpcHidChannel.readyState;
    if (!rpcHidProtocolVersion) return "handshaking";
    return `ready (v${rpcHidProtocolVersion}${rpcHidUnreliableReady ? "+u" : ""})`;
  }, [rpcHidChannel, rpcHidProtocolVersion, rpcHidUnreliableReady]);

  const sendMessage = useCallback(
    (
      message: RpcMessage,
      { ignoreHandshakeState, useUnreliableChannel, requireOrdered = true }: SendMessageParams = {},
    ) => {
      if (rpcHidChannel?.readyState !== "open") return;
      if (!rpcHidReady && !ignoreHandshakeState) return;

      let data: Uint8Array | undefined;
      try {
        data = message.marshal();
      } catch {
        return;
      }
      if (!data) return;

      if (useUnreliableChannel) {
        if (requireOrdered && rpcHidUnreliableReady) {
          rpcHidUnreliableChannel?.send(data as unknown as ArrayBuffer);
          return;
        }
        if (!requireOrdered && rpcHidUnreliableNonOrderedReady) {
          rpcHidUnreliableNonOrderedChannel?.send(data as unknown as ArrayBuffer);
          return;
        }
        // Some browsers do not establish the optional unordered data channel.
        // Falling back to the reliable HID channel is slower but must never
        // turn pointer input into a silent no-op.
      }

      rpcHidChannel?.send(data as unknown as ArrayBuffer);
    },
    [
      rpcHidChannel,
      rpcHidUnreliableChannel,
      rpcHidUnreliableNonOrderedChannel,
      rpcHidReady,
      rpcHidUnreliableReady,
      rpcHidUnreliableNonOrderedReady,
    ],
  );

  const reportKeyboardEvent = useCallback(
    (keys: number[], modifier: number) => {
      sendMessage(new KeyboardReportMessage(keys, modifier));
    },
    [sendMessage],
  );

  const reportKeypressEvent = useCallback(
    (key: number, press: boolean) => {
      sendMessage(new KeypressReportMessage(key, press));
    },
    [sendMessage],
  );

  const reportAbsMouseEvent = useCallback(
    (x: number, y: number, buttons: number) => {
      sendMessage(new PointerReportMessage(x, y, buttons), {
        // A133 converts browser absolute coordinates into relative composite
        // HID reports. Losing one coordinate makes every following delta use
        // the wrong baseline, so pointer input must use the reliable channel.
        // The fixed board backend understands this protocol even when a
        // long-lived browser loses only the handshake reply.
        ignoreHandshakeState: true,
      });
    },
    [sendMessage],
  );

  const reportRelMouseEvent = useCallback(
    (dx: number, dy: number, buttons: number) => {
      sendMessage(new MouseReportMessage(dx, dy, buttons), {
        ignoreHandshakeState: true,
      });
    },
    [sendMessage],
  );

  const reportWheelEvent = useCallback(
    (wheelY: number) => {
      sendMessage(new WheelReportMessage(wheelY), {
        ignoreHandshakeState: true,
      });
    },
    [sendMessage],
  );

  const reportKeyboardMacroEvent = useCallback(
    (isPaste: boolean, steps: KeyboardMacroStep[]) => {
      sendMessage(new KeyboardMacroReportMessage(isPaste, steps.length, steps));
    },
    [sendMessage],
  );

  const cancelOngoingKeyboardMacro = useCallback(() => {
    sendMessage(new CancelKeyboardMacroReportMessage());
  }, [sendMessage]);

  const reportKeypressKeepAlive = useCallback(() => {
    sendMessage(KEEPALIVE_MESSAGE);
  }, [sendMessage]);

  useEffect(() => {
    if (!rpcHidChannel) return;

    const messageHandler = (e: MessageEvent) => {
      if (typeof e.data === "string") {
        return;
      }

      const message = unmarshalHidRpcMessage(new Uint8Array(e.data));
      if (!message) return;
      if (message instanceof HandshakeMessage) return;
      onHidRpcMessage?.(message);
    };

    const errorHandler = (_e: Event) => {
      setRpcHidProtocolVersion(null);
    };

    rpcHidChannel.addEventListener("message", messageHandler);
    rpcHidChannel.addEventListener("error", errorHandler);

    return () => {
      rpcHidChannel.removeEventListener("message", messageHandler);
      rpcHidChannel.removeEventListener("error", errorHandler);
    };
  }, [rpcHidChannel, onHidRpcMessage, setRpcHidProtocolVersion]);

  return {
    reportKeyboardEvent,
    reportKeypressEvent,
    reportAbsMouseEvent,
    reportRelMouseEvent,
    reportWheelEvent,
    reportKeyboardMacroEvent,
    cancelOngoingKeyboardMacro,
    reportKeypressKeepAlive,
    rpcHidProtocolVersion,
    rpcHidReady,
    rpcHidStatus,
  };
}
