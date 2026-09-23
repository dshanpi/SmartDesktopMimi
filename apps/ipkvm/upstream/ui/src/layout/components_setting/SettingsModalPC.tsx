import React, { useState } from "react";
import {
  SettingOutlined,
  WifiOutlined,
  SafetyCertificateOutlined,
  DesktopOutlined,
  ToolOutlined,
  TagOutlined,
  VideoCameraOutlined,
} from "@ant-design/icons";
import { X } from "lucide-react";
import { useReactAt } from "i18n-auto-extractor/react";
import { cx } from "@/cva.config";

import SettingsAccessIndex from "@/layout/components_setting/access/AccessContent";
import SettingsGeneral from "@/layout/components_setting/general/GeneralContent";
import SettingsNetwork from "@/layout/components_setting/network/NetworkContent";
import SettingsHardware from "@/layout/components_setting/hardware/HardwareContent";
import SettingsAdvanced from "@/layout/components_setting/advanced/AdvancedContent";
import SettingsVersion from "@/layout/components_setting/version/VersionContent";
import VideoContent from "@/layout/components_setting/video/VideoContent";
import { useThemeSettings } from "@routes/login_page/useLocalAuth";
import { useUiStore } from "@/hooks/stores";

interface MenuItem {
  key: string;
  label: string;
  icon: React.ReactNode;
}

const menuItems: MenuItem[] = [
  { key: "general", label: "General", icon: <SettingOutlined /> },
  { key: "network", label: "Network", icon: <WifiOutlined /> },
  { key: "access", label: "Access", icon: <SafetyCertificateOutlined /> },
  { key: "hardware", label: "Hardware", icon: <DesktopOutlined /> },
  { key: "video", label: "Video", icon: <VideoCameraOutlined /> },
  { key: "advanced", label: "Advanced", icon: <ToolOutlined /> },
  { key: "version", label: "Version", icon: <TagOutlined /> },
];

const SettingsModalPC: React.FC = () => {
  const { $at } = useReactAt();
  const { isDark } = useThemeSettings();
  const [selectedMenu, setSelectedMenu] = useState<string>("general");
  const toggleSidebarView = useUiStore(state => state.toggleSidebarView);
  const setDisableFocusTrap = useUiStore(state => state.setDisableVideoFocusTrap);

  const handleMenuClick = (key: string) => {
    setSelectedMenu(key);
  };

  const handleClose = () => {
    toggleSidebarView("SettingsModal");
    setDisableFocusTrap(false);
  };

  const renderContent = () => {
    switch (selectedMenu) {
      case "general":
        return <SettingsGeneral />;
      case "network":
        return <SettingsNetwork />;
      case "access":
        return <SettingsAccessIndex />;
      case "hardware":
        return <SettingsHardware />;
      case "video":
        return <VideoContent />;
      case "advanced":
        return <SettingsAdvanced />;
      case "version":
        return <SettingsVersion />;
      default:
        return <SettingsGeneral />;
    }
  };

  return (
    <div className={cx(
      "fixed inset-0 z-[60] flex items-center justify-center",
      "bg-slate-950/55 p-4 backdrop-blur-sm dark:bg-black/75"
    )}>
      <div className={cx(
        "relative mx-auto h-[min(90dvh,920px)] w-full max-w-7xl",
        "flex flex-col",
        "bg-white dark:bg-slate-800",
        "rounded-2xl shadow-2xl ring-1 ring-white/20",
        "overflow-hidden"
      )}>
        {/* Header */}
        <div className={cx(
          "flex items-center justify-between px-6 py-4",
          "border-b border-slate-200 dark:border-slate-700",
          isDark ? "bg-slate-800" : "bg-white"
        )}>
          <h2 className={cx(
            "text-lg font-semibold",
            isDark ? "text-white" : "text-slate-900"
          )}>
            {$at("Settings")}
          </h2>
          <button
            type="button"
            onClick={handleClose}
            className={cx(
              "p-2 rounded-lg transition-colors",
              "hover:bg-slate-100 dark:hover:bg-slate-700",
              isDark ? "text-slate-400" : "text-slate-500"
            )}
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Content Area - fixed height, split layout */}
        <div className={cx(
          "flex flex-1 min-h-0",
          "md:grid md:grid-cols-[220px_minmax(0,1fr)]"
        )}>
          {/* Left Navigation */}
          <div className={cx(
            "w-full",
            "border-r border-slate-200 dark:border-slate-700",
            "overflow-y-auto",
            isDark ? "bg-slate-800" : "bg-slate-50"
          )}>
            <div className="flex flex-col gap-1 p-2">
              {menuItems.map(item => (
                <button
                  key={item.key}
                  type="button"
                  onClick={() => handleMenuClick(item.key)}
                  className={cx(
                    "w-full flex items-center gap-x-2 rounded-lg px-3 py-2.5 text-sm transition-colors",
                    "hover:bg-slate-100 dark:hover:bg-slate-700",
                    selectedMenu === item.key
                      ? "bg-blue-50 text-blue-700 dark:bg-blue-900/50 dark:text-blue-200"
                      : isDark ? "text-slate-300" : "text-slate-700"
                  )}
                >
                  <span className="shrink-0">{item.icon}</span>
                  <span>{$at(item.label)}</span>
                </button>
              ))}
            </div>
          </div>

          {/* Right Content */}
          <div className={cx(
            "w-full min-w-0",
            "overflow-y-auto",
            "bg-white dark:bg-slate-800"
          )}>
            <div className="p-6">
              {renderContent()}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

export default SettingsModalPC;
