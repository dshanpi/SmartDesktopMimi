import { useCallback, useEffect, useRef } from "react";

import { useHidRpc } from "@/hooks/useHidRpc";
import { useMouseStore, useSettingsStore, useVideoStore, useHidStore, HidState } from "@/hooks/stores";

import { usePointerLock } from "./usePointerLock";

export const useMouseEvents = (
    videoElm: React.RefObject<HTMLVideoElement>,
    pointerLock: ReturnType<typeof usePointerLock>,
    touchZoom?: {
      mobileScale: number;
      mobileTx: number;
      mobileTy: number;
      activeTouchPointers: React.MutableRefObject<Map<number, { x: number; y: number }>>;
      lastPanPoint: React.MutableRefObject<{ x: number; y: number } | null>;
    },
    disableTouchClick?: boolean,
    externalButtons = 0
  ) => {
    const { reportAbsMouseEvent, reportRelMouseEvent, reportWheelEvent } = useHidRpc();
    const wheelBlockedUntil = useRef(0);
    const lastAbsPosition = useRef<{ x: number; y: number } | null>(null);
    const settings = useSettingsStore();
    const { setMousePosition, setMouseMove } = useMouseStore();
    const { width: videoWidth, height: videoHeight } = useVideoStore();
    const isReinitializingGadget = useHidStore((state: HidState) => state.isReinitializingGadget);
    const usbState = useHidStore((state: HidState) => state.usbState);

    const clampDelta = (value: number) =>
      Math.max(-127, Math.min(127, Math.trunc(value)));

    const sendRelMouseMovement = useCallback(
      (x: number, y: number, buttons: number) => {
        if (settings.mouseMode !== "relative") return;
        // Don't send mouse events while reinitializing gadget
        if (isReinitializingGadget) return;
        if (usbState !== "configured") return;

        reportRelMouseEvent(clampDelta(x), clampDelta(y), buttons);
        setMouseMove({ x, y, buttons });
      },
      [reportRelMouseEvent, setMouseMove, settings.mouseMode, isReinitializingGadget, usbState],
    );

    const sendAbsMouseMovement = useCallback(
      (x: number, y: number, buttons: number) => {
        if (settings.mouseMode !== "absolute") return;
        // Don't send mouse events while reinitializing gadget
        if (isReinitializingGadget) return;
        if (usbState !== "configured") return;
        reportAbsMouseEvent(x, y, buttons);
        lastAbsPosition.current = { x, y };
        setMousePosition(x, y);
      },
      [reportAbsMouseEvent, setMousePosition, settings.mouseMode, isReinitializingGadget, usbState],
    );

  const relMouseMoveHandler = useCallback(
      (e: MouseEvent) => {
        const pt = (e as unknown as PointerEvent).pointerType as unknown as string;
        if (pt === "touch") {
          if (touchZoom) {
              const touchCount = touchZoom.activeTouchPointers.current.size;
              if (touchCount >= 2) return;
              if (touchZoom.mobileScale > 1 && touchZoom.lastPanPoint.current) return;
          }
        }
        
        // if(isMobile){
        //   e.preventDefault();
        // }
        if (settings.mouseMode !== "relative") return;
        // A desktop relative mouse is meaningful only while the browser owns
        // the pointer. Otherwise the visible local cursor and the remote host
        // cursor start at unrelated positions and drift exactly as two
        // independent mice. Touch input has no persistent browser cursor and
        // remains usable on devices without Pointer Lock.
        if (pt !== "touch" && !pointerLock.isPointerLockActive) return;

        const { buttons } = e;
        sendRelMouseMovement(e.movementX, e.movementY, buttons);
      },
      [pointerLock.isPointerLockActive, pointerLock.isPointerLockPossible, sendRelMouseMovement, settings.mouseMode, touchZoom],
    );

    const absMouseMoveHandler = useCallback(
      (e: MouseEvent) => {
        const pt = (e as unknown as PointerEvent).pointerType as unknown as string;
        if (pt === "touch") {
          if (touchZoom) {
              const touchCount = touchZoom.activeTouchPointers.current.size;
              const eventType = (e as unknown as PointerEvent).type;
              if (touchCount >= 2 && eventType !== "pointerup") return;
          }
        }

        //e.stopPropagation();
        // if(isMobile){
        //   e.preventDefault();
        // }

        const videoElmRefValue = videoElm.current;
        if (!videoElmRefValue) return;

        const rect = videoElmRefValue.getBoundingClientRect();
        if (!rect) return;

        if (settings.mouseMode !== "absolute") return;

        // video dimensions
        const displayedWidth = rect.width;
        const displayedHeight = rect.height;
        if (!displayedWidth || !displayedHeight) return;

        const videoElementAspectRatio = displayedWidth / displayedHeight;
        const videoStreamAspectRatio = videoWidth / videoHeight;

        let effectiveWidth = displayedWidth;
        let effectiveHeight = displayedHeight;
        let offsetX = 0;
        let offsetY = 0;

        if (videoElementAspectRatio > videoStreamAspectRatio) {
          effectiveWidth = displayedHeight * videoStreamAspectRatio;
          offsetX = (displayedWidth - effectiveWidth) / 2;
        } else if (videoElementAspectRatio < videoStreamAspectRatio) {
          effectiveHeight = displayedWidth / videoStreamAspectRatio;
          offsetY = (displayedHeight - effectiveHeight) / 2;
        }

        // Use visual coordinates relative to the video element's bounding rect
        const localX = e.clientX - rect.left;
        const localY = e.clientY - rect.top;

        const inputOffsetX = localX;
        const inputOffsetY = localY;

        const clampedX = Math.min(Math.max(offsetX, inputOffsetX), offsetX + effectiveWidth);
        const clampedY = Math.min(Math.max(offsetY, inputOffsetY), offsetY + effectiveHeight);

      const relativeX = (clampedX - offsetX) / effectiveWidth;
      const relativeY = (clampedY - offsetY) / effectiveHeight;

      // transform to HID absolute coordinates (0-32767 range)
      const x = Math.round(relativeX * 32767);
      const y = Math.round(relativeY * 32767);

      let buttons = e.buttons;

      if (pt === "touch") {
        const touchCount = touchZoom ? touchZoom.activeTouchPointers.current.size : 1;
        const pointerEvent = e as unknown as PointerEvent;
        const eventType = pointerEvent.type;

        if (eventType === "pointerup") {
          if (touchCount >= 2 || disableTouchClick) {
            buttons = 0;
          } else {
            buttons = 1;
          }
        } else {
          buttons = 0;
        }
      }

      buttons |= externalButtons;

      sendAbsMouseMovement(x, y, buttons);

      if (pt === "touch" && buttons !== externalButtons && (e as unknown as PointerEvent).type === "pointerup") {
        sendAbsMouseMovement(x, y, externalButtons);
      }
    },
    [settings.mouseMode, videoElm, videoWidth, videoHeight, sendAbsMouseMovement, touchZoom, disableTouchClick, externalButtons],
  );


  const mouseWheelHandler = useCallback(
    (e: WheelEvent) => {
      // Don't send wheel events while reinitializing gadget
      if (isReinitializingGadget) return;
      if (usbState !== "configured") return;
      e.stopPropagation();
      e.preventDefault();
      if (settings.scrollThrottling && Date.now() < wheelBlockedUntil.current) {
        return;
      }

      const isAccel = Math.abs(e.deltaY) >= 100;
      const accelScrollValue = e.deltaY / 100;
      const noAccelScrollValue = Math.sign(e.deltaY);
      const scrollValue = isAccel ? accelScrollValue : noAccelScrollValue;

      const clampedScrollValue = Math.max(-127, Math.min(127, scrollValue));
      const invertedScrollValue = -clampedScrollValue;

      reportWheelEvent(invertedScrollValue);

      if (settings.scrollThrottling)
        wheelBlockedUntil.current = Date.now() + settings.scrollThrottling;
    },
    [reportWheelEvent, settings, isReinitializingGadget, usbState],
  );

  const resetMousePosition = useCallback(() => {
    // Release buttons at the last reported coordinate. Sending (0, 0) here
    // would move an absolute HID pointer to the top-left corner on blur.
    const position = lastAbsPosition.current;
    if (position) sendAbsMouseMovement(position.x, position.y, 0);
  }, [sendAbsMouseMovement]);

  const isRelativeMouseMode = (settings.mouseMode === "relative");
  const mouseMoveHandler = isRelativeMouseMode ? relMouseMoveHandler : absMouseMoveHandler;
  const handlerRef = useRef(mouseMoveHandler);

  useEffect(() => {
    handlerRef.current = mouseMoveHandler;
  }, [mouseMoveHandler]);

  const {
    isPointerLockActive,
    isPointerLockPossible,
    requestPointerLock,
  } = pointerLock;

  const setupMouseEvents = useCallback(() => {
    const videoElmRefValue = videoElm.current;
    if (!videoElmRefValue) return;

    const abortController = new AbortController();
    const signal = abortController.signal;

    const eventHandler = (e: Event) => {
      if (e.type === "pointerdown") {
        videoElmRefValue.focus({ preventScroll: true });

        // Acquire Pointer Lock from the initial trusted pointer gesture.
        // Waiting for "click" is unreliable after a page reload because
        // pointer capture can consume/cancel the matching click in Chromium.
        // Do not forward this activation press to the host.
        const pointer = e as PointerEvent;
        if (
          pointer.pointerType !== "touch" &&
          isRelativeMouseMode &&
          isPointerLockPossible &&
          !isPointerLockActive &&
          !document.pointerLockElement
        ) {
          void requestPointerLock();
          return;
        }

        try {
          videoElmRefValue.setPointerCapture(pointer.pointerId);
        } catch {
          // Pointer capture is optional on older embedded browsers.
        }
      } else if (e.type === "pointerup") {
        const pointer = e as PointerEvent;
        if (videoElmRefValue.hasPointerCapture(pointer.pointerId))
          videoElmRefValue.releasePointerCapture(pointer.pointerId);
      }
      if (handlerRef.current) {
        handlerRef.current(e as any);
      }
    };

    videoElmRefValue.addEventListener("mousemove", eventHandler, { signal });
    videoElmRefValue.addEventListener("pointerdown", eventHandler, { signal });
    videoElmRefValue.addEventListener("pointerup", eventHandler, { signal });
    videoElmRefValue.addEventListener("wheel", mouseWheelHandler, {
      signal,
      passive: false,
    });

    if (isRelativeMouseMode) {
      videoElmRefValue.addEventListener("click",
        () => {
          if (isPointerLockPossible && !isPointerLockActive && !document.pointerLockElement) {
            void requestPointerLock();
          }
        },
        { signal },
      );
    } else {
      window.addEventListener("blur", resetMousePosition, { signal });
      document.addEventListener("visibilitychange", resetMousePosition, { signal });
    }

    const preventContextMenu = (e: MouseEvent) => e.preventDefault();
    videoElmRefValue.addEventListener("contextmenu", preventContextMenu, { signal });

    return () => {
      abortController.abort();
    };
  }, [
    videoElm,
    settings.mouseMode,
    isRelativeMouseMode,
    // Removed relMouseMoveHandler and absMouseMoveHandler from dependencies to prevent re-binding
    mouseWheelHandler,
    isPointerLockActive,
    isPointerLockPossible,
    requestPointerLock,
    resetMousePosition
  ]);

  return {
    setupMouseEvents,
  };
};
