import { LuBot, LuKeyboard, LuLayoutGrid, LuMaximize, LuMonitor, LuSettings } from "react-icons/lu";
import { FaKeyboard } from "react-icons/fa6";
import { useEffect } from "react";

import { keys, modifiers } from "@/keyboardMappings";

import CopeSvg from "@assets/second/copy.svg?react";
import OpenSvg from "@assets/second/open.svg?react";
import ZhongDuanSvg from "@assets/second/zhongduan.svg?react";
import HdmlSVG from "@assets/second/hdml.svg?react";
import Hdml2SVG from "@assets/second/hdml2.svg?react";
import UsbSVG from "@assets/second/usb.svg?react";
import Usb2SVG from "@assets/second/usb2.svg?react";
import MouseSVG from "@assets/second/mouse.svg?react";
import MediaSVG from "@assets/second/media.svg?react";
import SwichDirSvg from "@assets/second/swich_dri1.svg?react";
import SwichDirSvg2 from "@assets/second/swich_dir2.svg?react";
import Container from "@components/Container";
import Logo100Ask from "@assets/logo-100ask.png";
import { useReactAt } from "i18n-auto-extractor/react";

import { cx } from "@/cva.config";
import { useJsonRpc } from "@/hooks/useJsonRpc";
import {
  useAudioModeStore,
  useHidStore,
  useRTCStore,
  useSettingsStore,
  useUiStore,
  useUsbEpModeStore,
  useVideoStore,
} from "@/hooks/stores";
import KeyboardPanel from "@/layout/components_bottom/keyboard/KeyboardPanel";
import UsbEpModeSelect from "@/layout/components_bottom/usbepmode/UsbEpModeSelect";
import VolumeControl from "@components/VolumeControl";
import { useThemeSettings } from "@routes/login_page/useLocalAuth";
import { dark_bg2_style } from "@/layout/theme_color";
import { useFullscreenContext } from "@/layout/core/desktop/contexts/FullscreenContext";
import BottomPopoverButton from "@components/PopoverButton";
import MousePanel from "@components/MousePanel";

export default function TopBarPC() {
  const { requestFullscreen } = useFullscreenContext();
  const { $at } = useReactAt();
  const { isDark } = useThemeSettings();

  // Virtual Keyboard
  const virtualKeyboard = useHidStore(state => state.isVirtualKeyboardEnabled);
  const setVirtualKeyboard = useHidStore(state => state.setVirtualKeyboardEnabled);

  // UI Store
  const toggleSidebarView = useUiStore(state => state.toggleSidebarView);
  const setDisableFocusTrap = useUiStore(state => state.setDisableVideoFocusTrap);
  const terminalType = useUiStore(state => state.terminalType);
  const setTerminalType = useUiStore(state => state.setTerminalType);
  const sidebarView = useUiStore(state => state.sidebarView);
  const setSidebarView = useUiStore(state => state.setSidebarView);

  // Audio
  const setAudioMode = useAudioModeStore(state => state.setAudioMode);
  const audioMode = useAudioModeStore(state => state.audioMode);

  // USB EP Mode
  const usbEpMode = useUsbEpModeStore(state => state.usbEpMode);

  // HID Store
  const keyboardLedState = useHidStore(state => state.keyboardLedState);
  const usbState = useHidStore(state => state.usbState);

  // Video Store
  const hdmiState = useVideoStore(state => state.hdmiState);

  // RTC Store
  const isTurnServerInUse = useRTCStore(state => state.isTurnServerInUse);

  // Settings
  const showPressedKeys = useSettingsStore(state => state.showPressedKeys);
  const activeKeys = useHidStore(state => state.activeKeys);
  const activeModifiers = useHidStore(state => state.activeModifiers);

  // RPC
  const [send] = useJsonRpc();

  useEffect(() => {
    send("getAudioMode", {}, resp => {
      if ("error" in resp) return;
      setAudioMode(String(resp.result));
    });
  }, [send]);

  return (
    <div className="h-12">
      <Container className={cx(
        "border-b border-slate-200/50 dark:border-slate-700/50",
        "bg-white/80 dark:bg-slate-900/80",
        "backdrop-blur-md",
        dark_bg2_style
      )}>
        <div
          onKeyUp={e => e.stopPropagation()}
          onKeyDown={e => e.stopPropagation()}
          className="flex h-full items-center justify-between gap-4"
        >
          {/* ====== LEFT ZONE: Branding + Core Status ====== */}
          <div className="flex items-center gap-4">
            {/* Logo */}
            <a
              href="https://100ask.net/"
              target="_blank"
              rel="noopener noreferrer"
              className="flex items-center"
            >
              <img src={Logo100Ask} alt="" className="h-5 dark:hidden" />
              <img src={Logo100Ask} alt="" className="hidden h-5 dark:block" />
            </a>

            {/* Divider */}
            <div className="h-5 w-px bg-slate-300 dark:bg-slate-600" />

            {/* Core Status Pills */}
            <div className="flex items-center gap-1.5">
              {/* HDMI Status */}
              <StatusPill
                icon={hdmiState === "ready" ? <Hdml2SVG fontSize={14} /> : <HdmlSVG fontSize={14} />}
                label={$at("HDMI")}
                active={hdmiState === "ready"}
                activeColor="emerald"
              />

              {/* USB Status */}
              <StatusPill
                icon={usbState === "configured" ? <Usb2SVG fontSize={14} /> : <UsbSVG fontSize={14} />}
                label={$at("USB")}
                active={usbState === "configured"}
                activeColor="emerald"
              />
            </div>
          </div>

          {/* ====== CENTER ZONE: primary workspaces ====== */}
          <nav className="flex items-center gap-1 rounded-xl bg-slate-100 p-1 dark:bg-slate-800/80" aria-label="Primary workspaces">
            <ActionButton
              icon={<LuMonitor className="size-4" />}
              label={$at("Live")}
              onClick={() => {
                setSidebarView(null);
                setDisableFocusTrap(false);
              }}
              active={sidebarView === null}
            />
            <ActionButton
              icon={<LuBot className="size-4" />}
              label={$at("AI HDMI MCP")}
              onClick={() => {
                const opening = sidebarView !== "AgentWorkspace";
                setDisableFocusTrap(opening);
                setSidebarView(opening ? "AgentWorkspace" : null);
              }}
              active={sidebarView === "AgentWorkspace"}
            />
            <ActionButton
              icon={<LuLayoutGrid className="size-4" />}
              label={$at("Applications")}
              onClick={() => {
                const opening = sidebarView !== "ApplicationsWorkspace";
                setDisableFocusTrap(opening);
                setSidebarView(opening ? "ApplicationsWorkspace" : null);
              }}
              active={sidebarView === "ApplicationsWorkspace"}
            />
            <ActionButton
              icon={<LuSettings className="size-4" />}
              label={$at("Settings")}
              onClick={() => {
                const opening = sidebarView !== "SettingsModal";
                setDisableFocusTrap(opening);
                setSidebarView(opening ? "SettingsModal" : null);
              }}
              active={sidebarView === "SettingsModal"}
            />
          </nav>

          <div className="flex items-center gap-4">
            {/* Keyboard LED Popover - opens downward since we're in top bar */}
            <BottomPopoverButton
              buttonIconNode={
                <div className="flex items-center gap-2 text-xs text-slate-600 dark:text-slate-300">
                  <span className="text-slate-400">
                    {isDark ? <FaKeyboard fontSize={12} /> : <FaKeyboard fontSize={12} />}
                  </span>
                  <LedIndicator label="Num" active={keyboardLedState?.num_lock} />
                  <LedIndicator label="Caps" active={keyboardLedState?.caps_lock} />
                  <LedIndicator label="Scrl" active={keyboardLedState?.scroll_lock} />
                </div>
              }
              align="left"
              panelContent={<KeyboardPanel />}
            />

            {/* TURN Server indicator */}
            {isTurnServerInUse && (
              <div className="flex items-center gap-1.5 rounded-full bg-amber-100 dark:bg-amber-900/30 px-2.5 py-0.5 text-xs text-amber-700 dark:text-amber-300">
                <span className="size-1.5 rounded-full bg-amber-500" />
                {$at("Relayed by Cloudflare")}
              </div>
            )}

            {/* Pressed Keys Display */}
            {showPressedKeys && (
              <PressedKeysDisplay
                activeKeys={activeKeys}
                activeModifiers={activeModifiers}
              />
            )}
          </div>

          {/* ====== RIGHT ZONE: Actions ====== */}
          <div className="flex items-center gap-1">
            {/* Clipboard */}
            <ActionButton
              icon={<CopeSvg className="size-4" />}
              label={$at("Clipboard")}
              onClick={() => {
                setDisableFocusTrap(true);
                toggleSidebarView("Clipboard");
              }}
              active={sidebarView === "Clipboard"}
            />

            {/* Power */}
            <ActionButton
              icon={<OpenSvg className="size-4" />}
              label={$at("Power")}
              onClick={() => {
                setDisableFocusTrap(true);
                toggleSidebarView("PowerControl");
              }}
              active={sidebarView === "PowerControl"}
            />

            {/* Terminal */}
            <ActionButton
              icon={<ZhongDuanSvg className="size-4" />}
              label={$at("Terminal")}
              onClick={() => {
                setTerminalType(terminalType === "kvm" ? "none" : "kvm");
              }}
              active={terminalType === "kvm"}
              stopPropagation
            />

            {/* Virtual Keyboard */}
            <ActionButton
              icon={<LuKeyboard className="size-4" />}
              label={$at("Virtual Keyboard")}
              onClick={() => {
                setVirtualKeyboard(!virtualKeyboard);
              }}
              active={virtualKeyboard}
              hidden="lg"
            />

            {/* Fullscreen */}
            <ActionButton
              icon={<LuMaximize className="size-4" />}
              label={$at("Fullscreen")}
              onClick={() => requestFullscreen()}
            />

            {/* Divider */}
            <div className="mx-1 h-5 w-px bg-slate-300 dark:bg-slate-600" />

            {/* Mouse Panel - opens downward since we're in top bar */}
            <BottomPopoverButton
              buttonText={$at("Mouse")}
              buttonIconNode={<MouseSVG fontSize={14} />}
              align="right"
              panelContent={<MousePanel />}
            />

            {/* USB EP Mode */}
            <UsbEpModeSelect />

            {/* Volume */}
            {audioMode !== "disabled" && (
              <div className="hidden lg:flex">
                <VolumeControl size="XS" theme="light" />
              </div>
            )}

            {/* Virtual Media */}
            <ActionButton
              icon={<MediaSVG className="size-4" />}
              label={$at("Virtual Media")}
              onClick={() => {
                setDisableFocusTrap(true);
                toggleSidebarView("VirtualMedia");
              }}
              active={sidebarView === "VirtualMedia"}
            />

            {/* Shared Folders (only in MTP mode) */}
            {usbEpMode === "mtp" && (
              <ActionButton
                icon={isDark ? <SwichDirSvg2 className="size-4" /> : <SwichDirSvg className="size-4" />}
                label={$at("Shared Folders")}
                onClick={() => {
                  setDisableFocusTrap(true);
                  toggleSidebarView("SharedFolders");
                }}
                active={sidebarView === "SharedFolders"}
              />
            )}

            {/* Compose / Kana indicators */}
            {keyboardLedState?.compose && (
              <div className="rounded bg-slate-100 px-1.5 py-0.5 text-xs dark:bg-slate-800">{$at("Compose")}</div>
            )}
            {keyboardLedState?.kana && (
              <div className="rounded bg-slate-100 px-1.5 py-0.5 text-xs dark:bg-slate-800">{$at("Kana")}</div>
            )}

          </div>
        </div>
      </Container>
    </div>
  );
}

// ====== Sub-components ======

interface StatusPillProps {
  icon: React.ReactNode;
  label: string;
  active: boolean;
  activeColor?: "emerald" | "blue" | "red";
}

function StatusPill({ icon, label, active, activeColor = "emerald" }: StatusPillProps) {
  const colorMap = {
    emerald: "text-emerald-600 dark:text-emerald-400",
    blue: "text-blue-600 dark:text-blue-400",
    red: "text-red-600 dark:text-red-400",
  };

  return (
    <div className={cx(
      "inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-medium",
      "transition-colors duration-200",
      active
        ? "bg-emerald-50 text-emerald-700 dark:bg-emerald-900/30 dark:text-emerald-300"
        : "bg-slate-100 text-slate-500 dark:bg-slate-800 dark:text-slate-400"
    )}>
      <span className={cx(active && colorMap[activeColor])}>{icon}</span>
      <span>{label}</span>
    </div>
  );
}

interface LedIndicatorProps {
  label: string;
  active?: boolean;
}

function LedIndicator({ label, active }: LedIndicatorProps) {
  return (
    <div className="flex items-center gap-1">
      <span className={cx(
        "size-1.5 rounded-full transition-colors",
        active ? "bg-emerald-500" : "bg-slate-400 dark:bg-slate-600"
      )} />
      <span className="text-[10px] uppercase tracking-wide">{label}</span>
    </div>
  );
}

interface ActionButtonProps {
  icon: React.ReactNode;
  label: string;
  onClick: () => void;
  active?: boolean;
  hidden?: "lg" | "md" | "sm";
  stopPropagation?: boolean;
}

function ActionButton({ icon, label, onClick, active, hidden, stopPropagation }: ActionButtonProps) {
  return (
    <button
      type="button"
      onClick={stopPropagation ? (e) => { e.stopPropagation(); onClick(); } : onClick}
      onMouseDown={stopPropagation ? (e) => { e.stopPropagation(); } : undefined}
      className={cx(
        "inline-flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-xs font-medium",
        "text-slate-600 hover:bg-slate-100 hover:text-slate-900",
        "dark:text-slate-300 dark:hover:bg-slate-800 dark:hover:text-white",
        "transition-colors duration-150",
        active && "bg-slate-200 text-slate-900 dark:bg-slate-700 dark:text-white",
        hidden === "lg" && "hidden lg:inline-flex",
        hidden === "md" && "hidden md:inline-flex",
      )}
    >
      {icon}
      <span className="hidden xl:inline">{label}</span>
    </button>
  );
}

interface PressedKeysDisplayProps {
  activeKeys: any[];
  activeModifiers: any[];
}

function PressedKeysDisplay({ activeKeys, activeModifiers }: PressedKeysDisplayProps) {
  const keyNames = activeKeys.map(x => {
    const entry = Object.entries(keys).find(y => y[1] === x);
    return entry ? entry[0] : x;
  });

  const modNames = activeModifiers.map(x => {
    const entry = Object.entries(modifiers).find(y => y[1] === x);
    return entry ? entry[0] : x;
  });

  const allKeys = [...keyNames, ...modNames];

  return (
    <div className={cx(
      "flex items-center gap-1.5 rounded-full bg-slate-100 px-2.5 py-1 text-xs",
      "dark:bg-slate-800 dark:text-slate-300"
    )}>
      <span className="font-medium text-slate-500 dark:text-slate-400">Keys:</span>
      <span className="font-mono">{allKeys.join(" + ") || "—"}</span>
    </div>
  );
}
