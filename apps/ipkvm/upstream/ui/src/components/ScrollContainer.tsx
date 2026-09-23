import React from "react";
import { cx } from "@/cva.config";

interface ScrollContainerProps {
  children: React.ReactNode;
  className?: string;
  /** 是否在底部添加额外 padding，防止内容被底部导航遮挡 */
  bottomPadding?: boolean;
  /** 自定义滚动条颜色 */
  scrollbarColor?: "default" | "light" | "dark";
}

const scrollbarColorMap = {
  default: "scrollbar-thumb-slate-300 dark:scrollbar-thumb-slate-600",
  light: "scrollbar-thumb-slate-200",
  dark: "scrollbar-thumb-slate-700",
};

/**
 * 统一的滚动容器组件
 * - 支持触摸滚动（移动端惯性滚动）
 * - 支持自定义滚动条样式
 * - 底部 padding 防止内容被遮挡
 * - 隐藏水平滚动
 */
export function ScrollContainer({
  children,
  className,
  bottomPadding = true,
  scrollbarColor = "default",
}: ScrollContainerProps) {
  return (
    <div
      className={cx(
        "overflow-y-auto overflow-x-hidden",
        "h-full w-full",
        // iOS 惯性滚动支持
        "[-webkit-overflow-scrolling:touch]",
        // 滚动条样式
        "[&::-webkit-scrollbar]:w-2",
        "[&::-webkit-scrollbar-track]:bg-transparent",
        "[&::-webkit-scrollbar-thumb]:rounded-full",
        scrollbarColorMap[scrollbarColor],
        // Firefox 滚动条
        "scrollbar-thin",
        // 底部内边距
        bottomPadding && "pb-24",
        className
      )}
      // 启用触摸滚动
      style={{
        touchAction: "pan-y",
        WebkitOverflowScrolling: "touch",
      }}
    >
      {children}
    </div>
  );
}

export default ScrollContainer;
