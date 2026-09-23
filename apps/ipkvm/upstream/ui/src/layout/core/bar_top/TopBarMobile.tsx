import { LuBot, LuLayoutGrid, LuMaximize, LuWifi, LuWifiOff } from "react-icons/lu";
import OpenSvg from "@assets/second/open.svg?react";
import CopeSvg from "@assets/second/copy.svg?react";
import Setting2Svg from "@assets/second/set2.svg?react";
import Setting1Svg from "@assets/second/set1.svg?react";
import ZhongDuanSvg from "@assets/second/zhongduan.svg?react";
import ZhongDuanSvg2 from "@assets/second/zhongduan2.svg?react";
import HdmlSVG from "@assets/second/hdml.svg?react";
import Hdml2SVG from "@assets/second/hdml2.svg?react";
import UsbSVG from "@assets/second/usb.svg?react";
import Usb2SVG from "@assets/second/usb2.svg?react";
import Logo100Ask from "@assets/logo-100ask.png";
import Container from "@components/Container";
import { useHidStore, useUiStore, useVideoStore, useRTCStore } from "@/hooks/stores";
import { useFullscreenContext } from "@/layout/core/desktop/contexts/FullscreenContext";
import { useReactAt } from "i18n-auto-extractor/react";
import { cx } from "@/cva.config";

export default function MobileTopBar() {
  const { requestFullscreen } = useFullscreenContext();
  const setVirtualKeyboard = useHidStore(state => state.setVirtualKeyboardEnabled);
  const setDisableFocusTrap = useUiStore(state => state.setDisableVideoFocusTrap);
  const toggleTopBarView = useUiStore(state => state.toggleTopBarView);
  const toggleSidebarView = useUiStore(state => state.toggleSidebarView);
  const setSidebarView = useUiStore(state => state.setSidebarView);
  const sidebarView = useUiStore(state => state.sidebarView);
  const topBarView = useUiStore(state => state.topBarView);

  // Status from stores
  const hdmiState = useVideoStore(state => state.hdmiState);
  const usbState = useHidStore(state => state.usbState);
  const peerConnectionState = useRTCStore(state => state.peerConnectionState);
  const { $at } = useReactAt();

  const isConnected = peerConnectionState === "connected";

  const topBarButtons = [
    {
      id: "logo",
      icon: Logo,
      onClick: () => {
        setSidebarView(null);
        setDisableFocusTrap(false);
      },
      selected: sidebarView === null && topBarView === null,
    },
    {
      id: "status",
      icon: isConnected ? <LuWifi className="size-4 text-emerald-500" /> : <LuWifiOff className="size-4 text-slate-400" />,
      onClick: () => {
        // Toggle status popover
      },
      selected: false,
      showIndicator: true,
      indicatorColor: isConnected ? "bg-emerald-500" : "bg-slate-400",
    },
    {
      id: "agent",
      icon: <LuBot className="size-[18px]" />,
      onClick: () => {
        setDisableFocusTrap(true);
        setVirtualKeyboard(false);
        toggleSidebarView("AgentWorkspace");
      },
      selected: sidebarView === "AgentWorkspace",
    },
    {
      id: "apps",
      icon: <LuLayoutGrid className="size-[18px]" />,
      onClick: () => {
        setDisableFocusTrap(true);
        setVirtualKeyboard(false);
        toggleSidebarView("ApplicationsWorkspace");
      },
      selected: sidebarView === "ApplicationsWorkspace",
    },
    {
      id: "settings",
      icon: Setting2Svg,
      onClick: () => {
        setDisableFocusTrap(true);
        setVirtualKeyboard(false);
        toggleTopBarView("SettingsModal");
      },
      selected: topBarView === "SettingsModal",
    },
    {
      id: "clipboard",
      icon: CopeSvg,
      onClick: () => {
        setVirtualKeyboard(false);
        toggleTopBarView("ClipboardMobile");
      },
      selected: topBarView === "ClipboardMobile",
    },
    {
      id: "power",
      icon: OpenSvg,
      onClick: () => {
        setDisableFocusTrap(true);
        toggleSidebarView("PowerControl");
      },
      selected: sidebarView === "PowerControl",
    },
    {
      id: "terminal",
      icon: ZhongDuanSvg2,
      onClick: () => {
        setDisableFocusTrap(true);
        toggleSidebarView("TerminalTabsMobile");
      },
      selected: sidebarView === "TerminalTabsMobile",
      stopPropagation: true,
    },
  ];

  return (
    <div className="w-full">
      {/* Status Bar - Shows HDMI and USB status */}
      <div className={cx(
        "flex h-8 w-full items-center justify-center gap-4 px-4",
        "bg-white/90 dark:bg-slate-900/90",
        "border-b border-slate-200/50 dark:border-slate-700/50",
        "backdrop-blur-md"
      )}>
        <StatusBadge
          icon={hdmiState === "ready" ? <Hdml2SVG fontSize={12} /> : <HdmlSVG fontSize={12} />}
          label="HDMI"
          active={hdmiState === "ready"}
        />
        <StatusBadge
          icon={usbState === "configured" ? <Usb2SVG fontSize={12} /> : <UsbSVG fontSize={12} />}
          label="USB"
          active={usbState === "configured"}
        />
        <div className={cx(
          "rounded-full px-2 py-0.5 text-[10px] font-medium",
          isConnected
            ? "bg-emerald-50 text-emerald-600 dark:bg-emerald-900/30 dark:text-emerald-400"
            : "bg-slate-100 text-slate-500 dark:bg-slate-800 dark:text-slate-400"
        )}>
          {isConnected ? $at("Connected") : $at("Disconnected")}
        </div>
      </div>

      {/* Navigation Bar */}
      <div className={cx(
        "flex h-12 w-full flex-row items-center justify-around overflow-x-auto",
        "bg-white dark:bg-slate-900",
        "border-b border-slate-200 dark:border-slate-700"
      )}>
        {topBarButtons.map(button => (
          <div
            key={button.id}
            onClick={button.onClick}
            onMouseDown={button.stopPropagation ? (e: { stopPropagation: () => void }) => e.stopPropagation() : undefined}
            className={cx(
              "relative flex h-full min-w-12 flex-1 cursor-pointer items-center justify-center",
              "transition-colors duration-150",
              button.selected ? "text-emerald-600 dark:text-emerald-400" : "text-slate-600 dark:text-slate-300"
            )}
          >
            {typeof button.icon === 'function'
              ? <button.icon style={{ width: 18, height: 18 }} />
              : button.icon
            }
            {/* Selected indicator */}
            <div
              className={cx(
                "absolute bottom-0 left-1/2 -translate-x-1/2 h-0.5 w-6 rounded-full transition-all duration-200",
                button.selected ? "bg-emerald-500 opacity-100" : "bg-transparent"
              )}
            />
          </div>
        ))}

        {/* Fullscreen button - separated to right */}
        <div
          onClick={() => {
            setSidebarView(null);
            setVirtualKeyboard(false);
            requestFullscreen();
          }}
          className="relative flex h-full min-w-12 flex-1 cursor-pointer items-center justify-center text-slate-600 dark:text-slate-300"
        >
          <LuMaximize style={{ width: 18, height: 18 }} />
        </div>
      </div>
    </div>
  );
}

// ====== Sub-components ======

interface StatusBadgeProps {
  icon: React.ReactNode;
  label: string;
  active: boolean;
}

function StatusBadge({ icon, label, active }: StatusBadgeProps) {
  return (
    <div className={cx(
      "inline-flex items-center gap-1 rounded-full px-2 py-0.5 text-[10px] font-medium",
      "transition-colors duration-150",
      active
        ? "bg-emerald-50 text-emerald-600 dark:bg-emerald-900/30 dark:text-emerald-400"
        : "bg-slate-100 text-slate-500 dark:bg-slate-800 dark:text-slate-400"
    )}>
      <span className={cx(active && "text-emerald-600 dark:text-emerald-400")}>{icon}</span>
      <span>{label}</span>
    </div>
  );
}

function Logo() {
  return (
    <a
      href="https://100ask.net/"
      target="_blank"
      rel="noopener noreferrer"
      className="flex items-center"
    >
      <img src={Logo100Ask} alt="100ask Logo" className="h-4 dark:hidden" />
      <img src={Logo100Ask} alt="100ask Logo" className="hidden h-4 dark:block" />
    </a>
  );
}
