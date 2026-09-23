import { useMemo } from "react";

import { useVideoStore } from "@/hooks/stores";

import { useVideoStream } from "./useVideoStream";
import { usePointerLock } from "./usePointerLock";

export const useVideoOverlays = (
  videoStream: ReturnType<typeof useVideoStream>,
  pointerLock: ReturnType<typeof usePointerLock>,
  videoEffects: any
) => {
  const hdmiState = useVideoStore(state => state.hdmiState);
  const videoWidth = useVideoStore(state => state.width);
  const videoHeight = useVideoStore(state => state.height);

  const hdmiError = ["no_lock", "no_signal", "out_of_range"].includes(hdmiState);
  const isVideoLoading = !videoStream.isPlaying;

  const showPointerLockBar = useMemo(() => {
    if (videoEffects.settings.mouseMode !== "relative") return false;
    if (!pointerLock.isPointerLockPossible) return false;
    if (pointerLock.isPointerLockActive) return false;
    if (isVideoLoading) return false;
    if (!videoStream.isPlaying) return false;
    if (videoHeight === 0 || videoWidth === 0) return false;
    return true;
  }, [
    videoStream.isPlaying,
    pointerLock.isPointerLockActive,
    pointerLock.isPointerLockPossible,
    isVideoLoading,
    videoEffects.settings.mouseMode,
    videoHeight,
    videoWidth,
  ]);

  const showNoAutoplayOverlay = useMemo(() => {
    if (videoStream.peerConnectionState !== "connected") return false;
    if (!videoStream.autoplayBlocked) return false;
    if (videoStream.isPlaying) return false;
    if (hdmiError) return false;
    if (videoHeight === 0 || videoWidth === 0) return false;
    return true;
  }, [hdmiError, videoStream.autoplayBlocked, videoStream.isPlaying, videoStream.peerConnectionState, videoHeight, videoWidth]);

  const isWebRTCConnected = videoStream.peerConnectionState === "connected";
  const shouldHideVideo = hdmiError || (videoStream.peerConnectionState !== "connected" && isVideoLoading);
  const showConnectionOverlays = isWebRTCConnected;
  const showLoadingOverlay = isVideoLoading && !isWebRTCConnected;
  const showHDMIError = hdmiError;

  return {
    showPointerLockBar,
    showNoAutoplayOverlay,
    showConnectionOverlays,
    showLoadingOverlay,
    showHDMIError,
    shouldHideVideo,
    hdmiState,
  };
};
