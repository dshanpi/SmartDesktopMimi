import React, { useEffect, useRef } from "react";

import VirtualKeyboard from "@components/VirtualKeyboard";
import {
  HDMIErrorOverlay,
  LoadingVideoOverlay,
  NoAutoplayPermissionsOverlay,
  PointerLockBar,
} from "@components/VideoOverlay";
import IndexPc from "@/layout/components_bottom/terminal/index.pc";
import { cx } from "@/cva.config";
import { useVideoEffects } from "@/layout/core/desktop/hooks/useVideoEffects";
import { useVideoStream } from "@/layout/core/desktop/hooks/useVideoStream";
import { usePointerLock } from "@/layout/core/desktop/hooks/usePointerLock";
import { useKeyboardEvents } from "@/layout/core/desktop/hooks/useKeyboardEvents";
import { useMouseEvents } from "@/layout/core/desktop/hooks/useMouseEvents";
import { useVideoOverlays } from "@/layout/core/desktop/hooks/useVideoOverlays";
import { VideoContainer } from "@components/Video/VideoContainer";
import { VideoElement } from "@components/Video/VideoElement";
import SettingsModal from "@/layout/components_setting";
import { useUiStore, useHidStore, useRTCStore, useVideoStore } from "@/hooks/stores";
import { useHidRpc } from "@/hooks/useHidRpc";
import { useTouchZoom } from "@/layout/core/desktop/hooks/useTouchZoom";
import { usePasteHandler } from "@/layout/core/desktop/hooks/usePasteHandler";
import { useFullscreenContext } from "./contexts/FullscreenContext";
import WorkspacePanel from "./WorkspacePanel";

export default function PCDesktop() {
  const { videoElmRef, fullscreenContainerRef } = useFullscreenContext();
  const videoElm = videoElmRef;
  const audioElm = useRef<HTMLAudioElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const zoomContainerRef = useRef<HTMLDivElement>(null);
  const pasteCaptureRef = useRef<HTMLTextAreaElement>(null);

  const isVirtualKeyboardEnabled = useHidStore(state => state.isVirtualKeyboardEnabled);
  const isReinitializingGadget = useHidStore(state => state.isReinitializingGadget);
  const setTerminalType = useUiStore(state => state.setTerminalType);
  const terminalType = useUiStore(state => state.terminalType);
  const sidebarView = useUiStore(state => state.sidebarView);
  const setVirtualKeyboardEnabled = useHidStore(state => state.setVirtualKeyboardEnabled);

  useEffect(() => {
    if (isVirtualKeyboardEnabled) {
      setTerminalType("none");
    }
  }, [isVirtualKeyboardEnabled, setTerminalType]);

  useEffect(() => {
    if (terminalType !== "none") {
      setVirtualKeyboardEnabled(false);
    }
  }, [terminalType, setVirtualKeyboardEnabled]);

  const videoEffects = useVideoEffects();
  const videoStream = useVideoStream(videoElm as React.RefObject<HTMLVideoElement>, audioElm as React.RefObject<HTMLAudioElement>);
  const pointerLock = usePointerLock(videoElm as React.RefObject<HTMLVideoElement>);
  const touchZoom = useTouchZoom(zoomContainerRef as React.RefObject<HTMLDivElement>);
  const { handleGlobalPaste } = usePasteHandler(pasteCaptureRef as React.RefObject<HTMLTextAreaElement>);

  const keyboardEvents = useKeyboardEvents(
    pasteCaptureRef as React.RefObject<HTMLTextAreaElement>,
    isReinitializingGadget,
    videoElm as React.RefObject<HTMLVideoElement>,
  );
  const mouseEvents = useMouseEvents(videoElm as React.RefObject<HTMLVideoElement>, pointerLock, touchZoom);
  const overlays = useVideoOverlays(videoStream, pointerLock, videoEffects);
  const usbState = useHidStore(state => state.usbState);
  const peerConnectionState = useRTCStore(state => state.peerConnectionState);
  const { width: videoWidth, height: videoHeight, hdmiState } = useVideoStore();
  const { rpcHidReady, rpcHidStatus } = useHidRpc();

  const { setupKeyboardEvents } = keyboardEvents;
  const { setupVideoEventListeners } = videoStream;
  const { setupMouseEvents } = mouseEvents;

  useEffect(() => {
    const keyboardCleanup = setupKeyboardEvents();
    const videoCleanup = setupVideoEventListeners();
    const mouseCleanup = setupMouseEvents();
    return () => {
      keyboardCleanup?.();
      videoCleanup?.();
      mouseCleanup?.();
    };
  }, [setupKeyboardEvents, setupVideoEventListeners, setupMouseEvents]);

  const inputReady = usbState === "configured" && rpcHidReady;
  const streamReady = peerConnectionState === "connected" && hdmiState === "ready";

  return (
    <div className="relative h-full flex-1 overflow-hidden bg-slate-100 dark:bg-[#090d14]">
      <div className="flex h-full min-w-0">
      <div className="min-w-0 flex-1">
      <VideoContainer containerRef={containerRef as React.RefObject<HTMLDivElement>}>
        <div className="flex h-full min-h-0 flex-col">
          <PointerLockBar
            show={overlays.showPointerLockBar}
            onActivate={() => {
              videoElm.current?.focus({ preventScroll: true });
              void pointerLock.requestPointerLock();
            }}
          />

          <main className="h-0 min-h-0 flex-1 p-3 lg:p-4">
            <section className="flex h-full min-h-0 flex-col overflow-hidden rounded-2xl border border-slate-200 bg-white shadow-[0_18px_50px_-28px_rgba(15,23,42,.6)] dark:border-slate-700/70 dark:bg-slate-950">
              <header className="relative z-10 flex h-11 shrink-0 items-center justify-between gap-4 border-b border-slate-200 bg-white px-4 dark:border-slate-800 dark:bg-slate-950">
                <div className="flex min-w-0 items-center gap-3">
                  <span className={cx(
                    "inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-[11px] font-semibold tracking-wide",
                    streamReady
                      ? "bg-emerald-50 text-emerald-700 dark:bg-emerald-950 dark:text-emerald-300"
                      : "bg-amber-50 text-amber-700 dark:bg-amber-950 dark:text-amber-300",
                  )}>
                    <span className={cx(
                      "size-1.5 rounded-full",
                      streamReady ? "bg-emerald-500" : "bg-amber-500",
                    )} />
                    {streamReady ? "LIVE" : "CONNECTING"}
                  </span>
                  <span className="truncate text-xs font-medium text-slate-600 dark:text-slate-300">
                    HDMI {videoWidth > 0 && videoHeight > 0 ? `${videoWidth} × ${videoHeight}` : "—"}
                  </span>
                </div>
                <div className="flex items-center gap-2 text-[11px]">
                  <span className={cx(
                    "rounded-full px-2.5 py-1 font-medium",
                    usbState === "configured"
                      ? "bg-blue-50 text-blue-700 dark:bg-blue-950 dark:text-blue-300"
                      : "bg-slate-100 text-slate-500 dark:bg-slate-900 dark:text-slate-400",
                  )}>
                    USB {usbState}
                  </span>
                  <span
                    className={cx(
                      "rounded-full px-2.5 py-1 font-medium",
                      rpcHidReady
                        ? "bg-emerald-50 text-emerald-700 dark:bg-emerald-950 dark:text-emerald-300"
                        : "bg-slate-100 text-slate-500 dark:bg-slate-900 dark:text-slate-400",
                    )}
                    title={rpcHidStatus}
                    data-testid="hid-channel-state"
                  >
                    HID {rpcHidReady ? "READY" : rpcHidStatus}
                  </span>
                </div>
              </header>

              <div
                ref={fullscreenContainerRef}
                className={cx(
                  "relative flex min-h-0 flex-1 items-center justify-center overflow-hidden bg-[#090b10] outline-none",
                  {
                    "cursor-none": videoEffects.settings.isCursorHidden,
                  },
                )}
                data-testid="video-stage"
              >
                <div
                  className="relative flex h-full w-full items-center justify-center overflow-hidden bg-black focus-within:ring-2 focus-within:ring-inset focus-within:ring-blue-500"
                  data-testid="video-frame"
                >
                      <div
                        ref={zoomContainerRef}
                        className="flex h-full w-full items-center justify-center"
                        style={{
                          transform: `translate(${touchZoom.mobileTx}px, ${touchZoom.mobileTy}px) scale(${touchZoom.mobileScale})`,
                          transformOrigin: "center center",
                          touchAction: "none",
                        }}
                      >
                        <VideoElement
                          ref={videoElm}
                          onPlaying={videoStream.onVideoPlaying}
                          style={{...videoEffects.videoStyle, opacity: 1}}
                          className={cx(
                            "h-full w-full object-contain opacity-100 outline-none",
                            {
                              "opacity-60!": overlays.showPointerLockBar,
                            },
                          )}
                        />
                      </div>

                      {(videoStream.peerConnectionState === "connected") && (
                        <div
                          style={{ animationDuration: "500ms" }}
                          className="animate-slideUpFade pointer-events-none absolute inset-0 flex items-center justify-center"
                        >
                          <div className="relative h-full w-full rounded-md">
                            <LoadingVideoOverlay show={overlays.showLoadingOverlay} />
                            <HDMIErrorOverlay show={overlays.showHDMIError} hdmiState={overlays.hdmiState} />
                            <NoAutoplayPermissionsOverlay
                              show={overlays.showNoAutoplayOverlay}
                              onPlayClick={videoStream.handlePlayClick}
                            />
                          </div>
                        </div>
                      )}
                </div>
              </div>

              <footer
                className={cx(
                  "relative z-10 flex min-h-10 shrink-0 items-center justify-between gap-3 border-t px-4 text-xs",
                  inputReady
                    ? "border-emerald-100 bg-emerald-50/70 text-emerald-800 dark:border-emerald-900/60 dark:bg-emerald-950/40 dark:text-emerald-200"
                    : "border-amber-100 bg-amber-50/70 text-amber-800 dark:border-amber-900/60 dark:bg-amber-950/40 dark:text-amber-200",
                )}
                data-testid="input-readiness"
              >
                <span className="font-medium">
                  {inputReady && pointerLock.isPointerLockActive
                    ? "键盘鼠标透传已启用；按 Esc 可释放本机鼠标"
                    : inputReady
                    ? "点击画面或上方按钮，启用键盘鼠标透传"
                    : "键鼠暂不可用：请检查 USB 连接和 HID 通道"}
                </span>
                <span className="hidden shrink-0 text-[11px] opacity-75 md:inline">
                  输入焦点离开画面时自动释放按键
                </span>
              </footer>
            </section>
          </main>

          <VirtualKeyboard />
          <IndexPc />
        </div>
      </VideoContainer>
      </div>
      <WorkspacePanel />
      </div>
      {sidebarView === "SettingsModal" && (
        <SettingsModal />
      )}
      <audio
        id="global-audio"
        ref={audioElm}
        autoPlay
        muted={true}
        controls={false}
      />

      <textarea
        ref={pasteCaptureRef}
        aria-hidden="true"
        style={{ position: "fixed", left: -9999, top: -9999, width: 1, height: 1, opacity: 0 }}
        onPaste={handleGlobalPaste}
      />
    </div>
  );
}
