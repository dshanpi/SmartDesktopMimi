import { useCallback, useEffect, useRef, useState } from "react";
import { useResizeObserver } from "usehooks-ts";

import { useRTCStore, useVideoStore } from "@/hooks/stores";

export const useVideoStream = (
  videoElm: React.RefObject<HTMLVideoElement>,
  audioElm: React.RefObject<HTMLAudioElement>
) => {
  const [isPlaying, setIsPlaying] = useState(false);
  const [autoplayBlocked, setAutoplayBlocked] = useState(false);
  const mediaStream = useRTCStore(state => state.mediaStream);
  const peerConnectionState = useRTCStore(state => state.peerConnectionState);
  const setVideoClientSize = useVideoStore(state => state.setClientSize);
  const setVideoSize = useVideoStore(state => state.setSize);
  const setRenderFps = useVideoStore(state => state.setRenderFps);
  const setRenderFrames = useVideoStore(state => state.setRenderFrames);

  const renderTrackerRef = useRef({
    active: false,
    lastSampleAt: 0,
    framesSinceSample: 0,
    totalFrames: 0,
  });

  const updateVideoSizeStore = useCallback((videoElm: HTMLVideoElement) => {
    setVideoClientSize(videoElm.clientWidth, videoElm.clientHeight);
    setVideoSize(videoElm.videoWidth, videoElm.videoHeight);
  }, [setVideoClientSize, setVideoSize]);

  const startRenderTracking = useCallback(() => {
    const el = videoElm.current;
    if (!el) return;

    const anyEl = el as unknown as { requestVideoFrameCallback?: (cb: (now: number) => void) => number };
    if (!anyEl.requestVideoFrameCallback) return;

    if (renderTrackerRef.current.active) return;
    renderTrackerRef.current.active = true;
    renderTrackerRef.current.lastSampleAt = performance.now();
    renderTrackerRef.current.framesSinceSample = 0;
    renderTrackerRef.current.totalFrames = 0;

    const cb = (now: number) => {
      if (!renderTrackerRef.current.active) return;

      renderTrackerRef.current.framesSinceSample += 1;
      renderTrackerRef.current.totalFrames += 1;

      const dt = now - renderTrackerRef.current.lastSampleAt;
      if (dt >= 1000) {
        const fps = (renderTrackerRef.current.framesSinceSample * 1000) / dt;
        setRenderFps(Math.round(fps));
        setRenderFrames(renderTrackerRef.current.totalFrames);
        renderTrackerRef.current.framesSinceSample = 0;
        renderTrackerRef.current.lastSampleAt = now;
      }

      anyEl.requestVideoFrameCallback?.(cb);
    };

    anyEl.requestVideoFrameCallback(cb);
  }, [setRenderFps, setRenderFrames, videoElm]);

  const stopRenderTracking = useCallback(() => {
    renderTrackerRef.current.active = false;
    setRenderFps(0);
    setRenderFrames(0);
  }, [setRenderFps, setRenderFrames]);

  const markAsPlaying = useCallback(() => {
    setIsPlaying(true);
    setAutoplayBlocked(false);
    if (videoElm.current) {
      updateVideoSizeStore(videoElm.current);
    }
    startRenderTracking();
  }, [startRenderTracking, updateVideoSizeStore, videoElm]);

  const onVideoPlaying = useCallback(() => {
    console.log('[RTP] ✅ 视频播放中 - 正在渲染视频帧');
    if (videoElm.current) {
      console.log('[RTP] 视频元素尺寸:', videoElm.current.videoWidth, 'x', videoElm.current.videoHeight);
      console.log('[RTP] 视频元素状态: paused =', videoElm.current.paused, ', ended =', videoElm.current.ended);
    }
    markAsPlaying();
  }, [markAsPlaying, videoElm]);

  const handlePlayClick = useCallback(() => {
    const el = videoElm.current;
    if (!el) return;
    el.play().then(
      () => setAutoplayBlocked(false),
      () => {},
    );
  }, [videoElm]);

  const videoKeyUpHandler = useCallback((e: KeyboardEvent) => {
    if (!videoElm.current) return;
    if (e.code === "Space" && videoElm.current.paused) {
      videoElm.current.play();
    }
  }, [videoElm]);

  useResizeObserver({
    ref: videoElm as React.RefObject<HTMLElement>,
    onResize: ({ width, height }) => {
      if (width && height && videoElm.current) {
        updateVideoSizeStore(videoElm.current);
      }
    },
  });

  const addStreamToVideoElm = useCallback((mediaStream: MediaStream) => {
    if (!videoElm.current) return;
    
    const currentVideo = videoElm.current;
    currentVideo.srcObject = mediaStream;
    currentVideo.style.opacity = '1';
    
    currentVideo.muted = true; 
    currentVideo.defaultMuted = true;
    currentVideo.playsInline = true;
    currentVideo.autoplay = true;
    
    currentVideo.play().catch((err) => {
      if (err?.name === "NotAllowedError") {
        setAutoplayBlocked(true);
      }
    });
    updateVideoSizeStore(currentVideo);
    startRenderTracking();
  }, [startRenderTracking, updateVideoSizeStore, videoElm]);

  const addStreamToAudioElm = useCallback((mediaStream: MediaStream) => {
    if (!audioElm.current) return;
    audioElm.current.srcObject = mediaStream;
    audioElm.current.muted = true;
  }, [audioElm]);

  const setupVideoEventListeners = useCallback(() => {
    const videoElmRefValue = videoElm.current;
    if (!videoElmRefValue) return;

    const abortController = new AbortController();
    const signal = abortController.signal;

    videoElmRefValue.addEventListener("keyup", videoKeyUpHandler, { signal });
    videoElmRefValue.addEventListener("playing", onVideoPlaying, { signal });
    videoElmRefValue.addEventListener("play", onVideoPlaying, { signal });

    return () => abortController.abort();
  }, [onVideoPlaying, videoKeyUpHandler, videoElm]);

  useEffect(() => {
    if (videoElm.current) updateVideoSizeStore(videoElm.current);
  }, [updateVideoSizeStore]);

  useEffect(() => {
    if (!mediaStream) {
      stopRenderTracking();
      return;
    }
    addStreamToVideoElm(mediaStream);
    addStreamToAudioElm(mediaStream);
  }, [mediaStream, addStreamToVideoElm, addStreamToAudioElm, stopRenderTracking, videoElm, audioElm]);

  useEffect(() => {
    return () => {
      stopRenderTracking();
    };
  }, [stopRenderTracking]);

  return {
    isPlaying,
    autoplayBlocked,
    peerConnectionState,
    onVideoPlaying,
    handlePlayClick,
    setupVideoEventListeners,
  };
};
