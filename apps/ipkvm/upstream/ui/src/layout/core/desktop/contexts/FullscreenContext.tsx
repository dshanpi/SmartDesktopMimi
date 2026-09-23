import { createContext, useCallback, useContext, useEffect, useRef } from "react";

import { usePointerLock } from "../hooks/usePointerLock";

interface FullscreenContextValue {
  requestFullscreen: () => Promise<void>;
  videoElmRef: React.RefObject<HTMLVideoElement | null>;
  fullscreenContainerRef: React.RefObject<HTMLDivElement | null>;
}

const FullscreenContext = createContext<FullscreenContextValue | null>(null);

export const useFullscreenContext = () => {
  const context = useContext(FullscreenContext);
  if (!context) {
    throw new Error("useFullscreenContext must be used within FullscreenProvider");
  }
  return context;
};

export const FullscreenProvider = ({ children }: { children: React.ReactNode }) => {
  const videoElmRef = useRef<HTMLVideoElement>(null);
  const fullscreenContainerRef = useRef<HTMLDivElement>(null);
  const pointerLock = usePointerLock(videoElmRef);

  const isFullscreenEnabled = document.fullscreenEnabled;

  const requestKeyboardLock = useCallback(async () => {
    if (!videoElmRef.current) return;

    if ("keyboard" in navigator) {
      try {
        // @ts-expect-error - keyboard lock API
        await navigator.keyboard.lock();
      } catch {
        // ignore errors
      }
    }
  }, []);

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
    if (!isFullscreenEnabled || !fullscreenContainerRef.current) return;

    // 先锁定键盘（避免退出全屏时出现多条提示信息）
    await requestKeyboardLock();
    await pointerLock.requestPointerLock();

    await fullscreenContainerRef.current.requestFullscreen({
      navigationUI: "show",
    });
  }, [isFullscreenEnabled, pointerLock, requestKeyboardLock]);

  // 退出全屏时释放键盘锁定
  useEffect(() => {
    const handleFullscreenChange = () => {
      if (!document.fullscreenElement) {
        releaseKeyboardLock();
      }
    };

    document.addEventListener("fullscreenchange", handleFullscreenChange);
    return () => document.removeEventListener("fullscreenchange", handleFullscreenChange);
  }, [releaseKeyboardLock]);

  return (
    <FullscreenContext.Provider value={{ requestFullscreen, videoElmRef, fullscreenContainerRef }}>
      {children}
    </FullscreenContext.Provider>
  );
};
