import React, { useEffect, useState } from "react";
import { isMobile } from "react-device-detect";
import { useReactAt } from "i18n-auto-extractor/react";
import { LuMouse, LuMove, LuClock } from "react-icons/lu";

import ScrollThrottlingSelect, { Option } from "@components/ScrollThrottlingSelect";
import { useSettingsStore } from "@/hooks/stores";
import { useFeatureFlag } from "@/hooks/useFeatureFlag";
import { useJsonRpc } from "@/hooks/useJsonRpc";
import notifications from "@/notifications";
import { cx } from "@/cva.config";
import { useThemeSettings } from "@routes/login_page/useLocalAuth";

const scrollThrottlingOptions = [
  { value: "0", label: "Off" },
  { value: "10", label: "Low" },
  { value: "25", label: "Medium" },
  { value: "50", label: "High" },
  { value: "100", label: "Very High" },
];

const inputModeOptions: Option[] = [
  { value: "absolute", label: "Absolute" },
  { value: "relative", label: "Relative" },
];

const othersOptions: Option[] = [
  { value: "hide-cursor", label: "Hide Cursor" },
  { value: "jiggler", label: "Mouse Jiggler" },
];

const MousePanel: React.FC = () => {
  const { $at } = useReactAt();
  const { isDark } = useThemeSettings();
  const hideCursor: boolean = useSettingsStore(state => state.isCursorHidden);
  const setHideCursor = useSettingsStore(state => state.setCursorVisibility);
  const { isEnabled: isScrollSensitivityEnabled } = useFeatureFlag("0.3.8");
  const [send] = useJsonRpc();
  const [others, setOthers] = useState<string[]>([]);

  useEffect(() => {
    send("getJigglerState", {}, (resp) => {
      if (!("error" in resp) && resp.result) {
        setOthers((prevItems: string[]) => [...prevItems, "jiggler"]);
      } else {
        setOthers((prevItems) => prevItems.filter(item => item !== "jiggler"));
      }
    });
  }, [isScrollSensitivityEnabled, send]);

  useEffect(() => {
    if (hideCursor) {
      setOthers((prevItems: string[]) => [...prevItems, "hide-cursor"]);
    } else {
      setOthers((prevItems) => prevItems.filter(item => item !== "hide-cursor"));
    }
  }, [hideCursor]);

  const handleOtherChange = (data: string[] | string) => {
    if (data.includes("hide-cursor") != others.includes("hide-cursor")) {
      handlehideCursorChange(data.includes("hide-cursor"));
    }
    if (data.includes("jiggler") != others.includes("jiggler")) {
      handleJigglerChange(data.includes("jiggler"));
    }
  };

  const handlehideCursorChange = (enabled: boolean) => {
    setHideCursor(enabled);
  };

  const handleJigglerChange = (enabled: boolean) => {
    send("setJigglerState", { enabled }, resp => {
      if ("error" in resp) {
        notifications.error(
          `Failed to set jiggler state: ${resp.error.data || "Unknown error"}`,
        );
      } else {
        if (enabled) {
          setOthers((prevItems: string[]) => [...prevItems, "jiggler"]);
        } else {
          setOthers((prevItems) => prevItems.filter(item => item !== "jiggler"));
        }
      }
    });
  };

  const mouseMode = useSettingsStore(state => state.mouseMode);
  const setMouseMode = useSettingsStore(state => state.setMouseMode);
  const [modeData, setModeData] = useState<string>(mouseMode);

  useEffect(() => {
    setModeData(mouseMode);
  }, [mouseMode]);

  const handleModeChange = (data: string[] | string) => {
    setMouseMode(data as string);
  };

  const scrollThrottling = useSettingsStore(state => state.scrollThrottling);
  const setScrollThrottling = useSettingsStore(state => state.setScrollThrottling);
  const [scrollData, setScrollData] = useState<string>(String(scrollThrottling));

  useEffect(() => {
    setScrollData(String(scrollThrottling));
  }, [scrollThrottling]);

  const handleScrollChange = (data: string[] | string) => {
    setScrollThrottling(Number(data as string));
  };

  if (isMobile) {
    return (
      <div className={cx(
        "w-full h-full flex flex-col",
        isDark ? "bg-slate-900 text-white" : "bg-white text-slate-900"
      )}>
        <div className="flex flex-col w-full mx-auto">
          <div className="px-5 pt-5">
            <ScrollThrottlingSelect
              mode="single"
              title={$at("Scroll Throttling")}
              options={scrollThrottlingOptions}
              value={scrollData}
              onChange={handleScrollChange}
            />
          </div>

          <div className={cx("h-px mx-5 my-4", isDark ? "bg-slate-700" : "bg-slate-200")} />

          <div className="px-5">
            <ScrollThrottlingSelect
              mode="single"
              title={$at("Input Modes")}
              options={inputModeOptions}
              value={modeData}
              onChange={handleModeChange}
            />
          </div>

          <div className={cx("h-px mx-5 my-4", isDark ? "bg-slate-700" : "bg-slate-200")} />

          <div className="px-5 pb-5">
            <ScrollThrottlingSelect
              mode="multiple"
              title={$at("Others")}
              options={othersOptions}
              value={others}
              onChange={handleOtherChange}
            />
          </div>
        </div>
      </div>
    );
  }

  // Desktop version - Card-based layout
  return (
    <div className={cx(
      "w-[250px] overflow-hidden",
      "bg-white dark:bg-slate-900"
    )}>
      {/* Header */}
      <div className={cx(
        "flex items-center gap-3 px-4 py-3",
        "border-b border-slate-100 dark:border-slate-800",
        isDark ? "bg-slate-800/50" : "bg-slate-50"
      )}>
        <div className={cx(
          "flex items-center justify-center w-8 h-8 rounded-lg",
          "bg-emerald-100 dark:bg-emerald-900/30"
        )}>
          <LuMouse className="w-4 h-4 text-emerald-600 dark:text-emerald-400" />
        </div>
        <div>
          <div className={cx(
            "text-sm font-semibold",
            isDark ? "text-white" : "text-slate-900"
          )}>
            {$at("Mouse Settings")}
          </div>
          <div className={cx(
            "text-xs",
            isDark ? "text-slate-400" : "text-slate-500"
          )}>
            {$at("Configure mouse behavior")}
          </div>
        </div>
      </div>

      {/* Content */}
      <div className="p-4 space-y-5">
        {/* Scroll Throttling */}
        <ScrollThrottlingSelect
          mode="single"
          title={$at("Scroll Throttling")}
          options={scrollThrottlingOptions}
          value={scrollData}
          onChange={handleScrollChange}
        />

        <div className={cx("h-px -mx-4", isDark ? "bg-slate-700" : "bg-slate-100")} />

        {/* Input Mode */}
        <div>
          <div className="flex items-center gap-2 mb-2">
            <LuMove className={cx("w-3.5 h-3.5", isDark ? "text-slate-500" : "text-slate-400")} />
            <div className={cx(
              "text-[11px] font-semibold uppercase tracking-wider",
              isDark ? "text-slate-400" : "text-slate-500"
            )}>
              {$at("Input Modes")}
            </div>
          </div>
          <ScrollThrottlingSelect
            mode="single"
            title=""
            options={inputModeOptions}
            value={modeData}
            onChange={handleModeChange}
          />
        </div>

        <div className={cx("h-px -mx-4", isDark ? "bg-slate-700" : "bg-slate-100")} />

        {/* Others */}
        <div>
          <div className="flex items-center gap-2 mb-2">
            <LuClock className={cx("w-3.5 h-3.5", isDark ? "text-slate-500" : "text-slate-400")} />
            <div className={cx(
              "text-[11px] font-semibold uppercase tracking-wider",
              isDark ? "text-slate-400" : "text-slate-500"
            )}>
              {$at("Options")}
            </div>
          </div>
          <ScrollThrottlingSelect
            mode="multiple"
            title=""
            options={othersOptions}
            value={others}
            onChange={handleOtherChange}
          />
        </div>
      </div>
    </div>
  );
};

export default MousePanel;
