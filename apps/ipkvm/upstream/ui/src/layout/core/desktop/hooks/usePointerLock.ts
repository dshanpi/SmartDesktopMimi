import { useCallback, useEffect, useState } from "react";

import { useSettingsStore } from "@/hooks/stores";

export const usePointerLock = (videoElm: React.RefObject<HTMLVideoElement | null>) => {
  const [isPointerLockActive, setIsPointerLockActive] = useState(false);
  const settings = useSettingsStore();
  // Pointer Lock is not a secure-context-only API. The device UI is normally
  // served from an HTTP LAN address, so protocol-based gating leaves the
  // browser cursor visible while relative HID reports move a different cursor
  // on the host. Detect the API itself instead.
  const isPointerLockPossible =
    typeof Element !== "undefined" &&
    "requestPointerLock" in Element.prototype;

  const requestPointerLock = useCallback(async () => {
    if (!isPointerLockPossible || !videoElm.current || document.pointerLockElement) return;

    if (settings.mouseMode === "relative") {
      try {
        // requestPointerLock itself is the permission prompt. Requiring the
        // Permissions API to already say "granted" prevented the first click
        // from ever entering relative mode in normal browsers.
        await videoElm.current.requestPointerLock();
      } catch {
        // The browser can reject this when it was not called from a user
        // gesture; the next click will retry.
      }
    }
  }, [isPointerLockPossible, settings.mouseMode, videoElm]);

  useEffect(() => {
    if (!isPointerLockPossible || !videoElm.current) return;

    const handlePointerLockChange = () => {
      setIsPointerLockActive(document.pointerLockElement === videoElm.current);
    };
    const handlePointerLockError = () => setIsPointerLockActive(false);

    document.addEventListener("pointerlockchange", handlePointerLockChange);
    document.addEventListener("pointerlockerror", handlePointerLockError);
    handlePointerLockChange();
    return () => {
      document.removeEventListener("pointerlockchange", handlePointerLockChange);
      document.removeEventListener("pointerlockerror", handlePointerLockError);
    };
  }, [isPointerLockPossible, videoElm]);

  useEffect(() => {
    if (
      settings.mouseMode !== "relative" &&
      document.pointerLockElement === videoElm.current
    ) {
      document.exitPointerLock();
    }
  }, [settings.mouseMode, videoElm]);

  return {
    isPointerLockActive,
    isPointerLockPossible,
    requestPointerLock,
  };
};
