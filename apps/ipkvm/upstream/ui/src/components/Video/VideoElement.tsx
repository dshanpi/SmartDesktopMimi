import { forwardRef } from "react";
import { isMobile } from 'react-device-detect';

import { useHidStore, HidState } from "@/hooks/stores";

interface VideoElementProps {
  onPlaying: () => void;
  style: React.CSSProperties;
  className: string;
}

export const VideoElement = forwardRef<HTMLVideoElement, VideoElementProps>(
  ({ onPlaying, style, className }, ref) => {
    const setVirtualKeyboardEnabled = useHidStore((state: HidState) => state.setVirtualKeyboardEnabled);
    const isVirtualKeyboardEnabled = useHidStore((state: HidState) => state.isVirtualKeyboardEnabled);

    const handleClick = () => {
      if (isMobile && !isVirtualKeyboardEnabled) {
        setVirtualKeyboardEnabled(true);
      }
    };

    const mergedStyle = {
      ...style,
    };

    const handlePlaying = (e: React.SyntheticEvent<HTMLVideoElement>) => {
      const video = e.currentTarget;
      console.log('[VIDEO] ✅ onPlaying 事件触发 - 视频正在播放');
      console.log('[VIDEO] videoWidth:', video.videoWidth, 'videoHeight:', video.videoHeight);
      console.log('[VIDEO] video.readyState:', video.readyState);
      console.log('[VIDEO] video.paused:', video.paused);
      console.log('[VIDEO] video.ended:', video.ended);
      console.log('[VIDEO] video.error:', video.error);
      onPlaying();
    };

    const handleError = (e: React.SyntheticEvent<HTMLVideoElement>) => {
      const video = e.currentTarget;
      console.error('[VIDEO] ❌ onError 事件触发');
      console.error('[VIDEO] video.error:', video.error);
      console.error('[VIDEO] video.error.code:', video.error?.code);
      console.error('[VIDEO] video.error.message:', video.error?.message);
    };

    const handleWaiting = (_e: React.SyntheticEvent<HTMLVideoElement>) => {
      console.log('[VIDEO] ⏳ onWaiting 事件触发 - 等待更多数据');
    };

    const handleCanPlay = (e: React.SyntheticEvent<HTMLVideoElement>) => {
      const video = e.currentTarget;
      console.log('[VIDEO] onCanPlay 事件触发');
      console.log('[VIDEO] videoWidth:', video.videoWidth, 'videoHeight:', video.videoHeight);
      console.log('[VIDEO] srcObject:', video.srcObject);
      console.log('[VIDEO] paused:', video.paused);
      // Try to ensure video is playing
      if (video.paused) {
        video.play().catch(() => {});
      }
    };

    const handleLoadedMetadata = (e: React.SyntheticEvent<HTMLVideoElement>) => {
      const video = e.currentTarget;
      console.log('[VIDEO] onLoadedMetadata 事件触发');
      console.log('[VIDEO] videoWidth:', video.videoWidth, 'videoHeight:', video.videoHeight);
      console.log('[VIDEO] duration:', video.duration);
      console.log('[VIDEO] video.srcObject:', video.srcObject);
      console.log('[VIDEO] video.readyState:', video.readyState);
      console.log('[VIDEO] video.networkState:', video.networkState);
      // Try to play if not playing
      if (video.paused) {
        console.log('[VIDEO] Video is paused, attempting to play...');
        video.play().catch(err => console.log('[VIDEO] play() failed:', err));
      }
    };

    return (
      <video
        ref={ref}
        tabIndex={0}
        data-testid="kvm-video"
        aria-label="KVM remote display; click to control keyboard and mouse"
        autoPlay={true}
        muted={true}
        controls={false}
        onPlaying={handlePlaying}
        onPlay={handlePlaying}
        onClick={handleClick}
        onError={handleError}
        onWaiting={handleWaiting}
        onCanPlay={handleCanPlay}
        onLoadedMetadata={handleLoadedMetadata}
        playsInline
        disablePictureInPicture
        controlsList="nofullscreen"
        style={mergedStyle}
        className={className}
      />
    );
  }
);

VideoElement.displayName = "VideoElement";
