import { useCallback, useEffect } from "react";

import { usePointerLock } from "./usePointerLock";

export const useFullscreen = (
  videoElm: React.RefObject<HTMLVideoElement>,
  pointerLock: ReturnType<typeof usePointerLock>,
) => {
  const isFullscreenEnabled = document.fullscreenEnabled;

  const requestKeyboardLock = useCallback(async () => {
    if (!videoElm.current) return;

    if ("keyboard" in navigator) {
      try {
        // @ts-expect-error - keyboard lock API
        await navigator.keyboard.lock();
      } catch {
        // ignore errors
      }
    }
  }, [videoElm]);

  const releaseKeyboardLock = useCallback(async () => {
    if ("keyboard" in navigator) {
      try {
        // @ts-expect-error - keyboard unlock API
        await navigator.keyboard.unlock();
      } catch {
        // ignore errors
      }
    }
  }, []);

  const requestFullscreen = useCallback(async () => {
    if (!isFullscreenEnabled || !videoElm.current) return;

    await requestKeyboardLock();
    await pointerLock.requestPointerLock();

    await videoElm.current.requestFullscreen({
      navigationUI: "show",
    });
  }, [isFullscreenEnabled, requestKeyboardLock, pointerLock, videoElm]);

  // Release keyboard lock when exiting fullscreen
  useEffect(() => {
    const handleFullscreenChange = () => {
      if (!document.fullscreenElement) {
        releaseKeyboardLock();
      }
    };

    document.addEventListener("fullscreenchange", handleFullscreenChange);
    return () => document.removeEventListener("fullscreenchange", handleFullscreenChange);
  }, [releaseKeyboardLock]);

  return {
    requestFullscreen,
    releaseKeyboardLock,
  };
};