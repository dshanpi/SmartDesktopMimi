import { useMemo, useEffect, useState } from 'react';
import { useVideoStore } from '@/hooks/stores';

export interface VideoGeometry {
  aspectRatio: string | undefined;  // CSS aspect-ratio 值
  containerWidth: number;
  containerHeight: number;
  videoWidth: number;
  videoHeight: number;
  scaledWidth: number;   // 缩放后的宽度
  scaledHeight: number;   // 缩放后的高度
  offsetX: number;        // 居中偏移 X
  offsetY: number;        // 居中偏移 Y
}

/**
 * PiKVM 风格的视频几何计算 Hook
 *
 * 实现 4 层机制:
 * - Layer 2: 动态设置容器 aspect-ratio CSS
 * - Layer 3: JavaScript 几何计算 (Math.min 缩放)
 * - Layer 4: 窗口最大化时的智能处理 (通过 aspect-ratio + max-width/height)
 */
export const useVideoAspectRatio = (
  containerRef: React.RefObject<HTMLDivElement | null>
): VideoGeometry => {
  const { width: videoWidth, height: videoHeight } = useVideoStore();

  // 监听容器尺寸变化
  const [containerSize, setContainerSize] = useState({ width: 0, height: 0 });

  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;

    const updateSize = () => {
      setContainerSize({
        width: container.clientWidth,
        height: container.clientHeight,
      });
    };

    // 初始更新
    updateSize();

    // 监听 resize 事件
    const resizeObserver = new ResizeObserver(updateSize);
    resizeObserver.observe(container);

    return () => resizeObserver.disconnect();
  }, [containerRef]);

  return useMemo(() => {
    const containerWidth = containerSize.width;
    const containerHeight = containerSize.height;

    // Layer 3: 几何计算 (参考 PiKVM stream.js:158-178)
    // 使用 Math.min 确保视频完整显示（不裁剪）
    let scaledWidth = containerWidth;
    let scaledHeight = containerHeight;
    let offsetX = 0;
    let offsetY = 0;

    if (videoWidth && videoHeight && containerWidth && containerHeight) {
      const ratio = Math.min(
        containerWidth / videoWidth,
        containerHeight / videoHeight
      );

      scaledWidth = Math.round(ratio * videoWidth);
      scaledHeight = Math.round(ratio * videoHeight);

      // 居中偏移
      offsetX = Math.round((containerWidth - scaledWidth) / 2);
      offsetY = Math.round((containerHeight - scaledHeight) / 2);
    }

    return {
      // Layer 2: aspect-ratio CSS
      aspectRatio: (videoWidth && videoHeight)
        ? `${videoWidth} / ${videoHeight}`
        : undefined,
      containerWidth,
      containerHeight,
      videoWidth,
      videoHeight,
      scaledWidth,
      scaledHeight,
      offsetX,
      offsetY,
    };
  }, [videoWidth, videoHeight, containerSize]);
};
