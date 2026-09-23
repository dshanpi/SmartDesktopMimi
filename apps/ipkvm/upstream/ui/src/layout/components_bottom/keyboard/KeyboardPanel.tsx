import React, { useCallback, useEffect, useMemo, useState } from "react";
import { Button } from "antd";
import LeftSVG from "@assets/second/left.svg?react";
import { isDesktop, isMobile } from "react-device-detect";
import { CloseOutlined } from '@ant-design/icons';
import { useReactAt } from "i18n-auto-extractor/react";
import { LuKeyboard, LuKeyboardOff, LuEye } from "react-icons/lu";

import ScrollThrottlingSelect, { Option } from "@components/ScrollThrottlingSelect";
import { layouts } from "@/keyboardLayouts";
import { KeyboardLedSync, useSettingsStore } from "@/hooks/stores";
import { useJsonRpc } from "@/hooks/useJsonRpc";
import notifications from "@/notifications";
import KeyboardLayoutModal, { KeyboardLayoutContent } from "@/layout/components_bottom/keyboard/KeyboardLayoutModal";
import { cx } from "@/cva.config";
import { useThemeSettings } from "@routes/login_page/useLocalAuth";

const KeyboardPanel: React.FC = () => {
  const { $at } = useReactAt();
  const { isDark } = useThemeSettings();
  const [showMore, setShowMore] = useState(false);
  const keyboardLayout = useSettingsStore(state => state.keyboardLayout);
  const setKeyboardLayout = useSettingsStore(state => state.setKeyboardLayout);

  const [layoutOptions, setLayoutOptions] = useState<Option[]>();
  const [maxShowCount, setMaxShowCount] = useState(3);

  useEffect(() => {
    const curLayoutOptions = (() => {
      const options = Object.entries(layouts).map(([code, language]) => ({
        value: code,
        label: language,
      }));

      const currentLayout = keyboardLayout ?? "";
      if (!currentLayout) {
        return options;
      }

      const currentIndex = options.findIndex(option => option.value === currentLayout);
      if (currentIndex === -1 || currentIndex < 3) {
        setMaxShowCount(3);
        return options;
      }
      setMaxShowCount(4);
      const [movedItem] = options.splice(currentIndex, 1);
      options.splice(3, 0, movedItem);
      return options;
    })();
    setLayoutOptions(curLayoutOptions);
  }, [layouts, keyboardLayout]);

  const safeKeyboardLayout = useMemo(() => {
    if (keyboardLayout && keyboardLayout.length > 0)
      return keyboardLayout;
    return "en_US";
  }, [keyboardLayout]);

  const [send] = useJsonRpc();

  useEffect(() => {
    send("getKeyboardLayout", {}, resp => {
      if ("error" in resp) return;
      setKeyboardLayout(resp.result as string);
    });
  }, []);

  const onKeyboardLayoutChange = useCallback(
    (layout: string[] | string) => {
      send("setKeyboardLayout", { layout }, resp => {
        if ("error" in resp) {
          notifications.error(
            `Failed to set keyboard layout: ${resp.error.data || "Unknown error"}`,
          );
        }
        notifications.success("Keyboard layout set successfully");
        setKeyboardLayout(layout as string);
      });
    },
    [send, setKeyboardLayout],
  );

  const keysOptionsList: Option[] = [
    { label: "Show Pressed Keys", value: "show-pressed-keys" },
  ];

  const showPressedKeys = useSettingsStore(state => state.showPressedKeys);
  const setShowPressedKeys = useSettingsStore(state => state.setShowPressedKeys);
  const [keysOptions, setKeysOptions] = useState<string[]>(["show-pressed-keys"]);

  useEffect(() => {
    if (showPressedKeys) {
      setKeysOptions((prevItems: string[]) => [...prevItems, "show-pressed-keys"]);
    } else {
      setKeysOptions((prevItems) => prevItems.filter(item => item !== "show-pressed-keys"));
    }
  }, [showPressedKeys]);

  const handleShowPressedChange = (data: string[] | string) => {
    if (data.includes("show-pressed-keys")) {
      setShowPressedKeys(true);
    } else {
      setShowPressedKeys(false);
    }
  };

  const ledSyncOptions: Option[] = [
    { value: "auto", label: "Auto" },
    { value: "browser", label: "Browser Only" },
    { value: "host", label: "Host Only" },
  ];

  const keyboardLedSync = useSettingsStore(state => state.keyboardLedSync);
  const setKeyboardLedSync = useSettingsStore(state => state.setKeyboardLedSync);
  const [ledSync, setLedSync] = useState<string>(keyboardLedSync);

  useEffect(() => {
    setLedSync(keyboardLedSync);
  }, [keyboardLedSync]);

  const handleLedChange = (data: string[] | string) => {
    setKeyboardLedSync(data as KeyboardLedSync);
  };

  // Mobile full layout view
  if (showMore && isMobile) {
    return (
      <div className={cx(
        "w-full h-full flex flex-col",
        isDark ? "bg-slate-900 text-white" : "bg-white text-slate-900"
      )}>
        <div className={cx(
          "flex justify-between items-center px-5 py-4",
          "border-b",
          isDark ? "border-slate-700" : "border-slate-200"
        )}>
          <div className={cx(
            "text-base font-semibold",
            isDark ? "text-white" : "text-slate-900"
          )}>
            Keyboard Layout
          </div>
          <Button
            type="text"
            icon={<CloseOutlined />}
            onClick={() => setShowMore(false)}
            className="text-slate-400"
          />
        </div>
        <KeyboardLayoutContent
          value={safeKeyboardLayout}
          onChange={onKeyboardLayoutChange}
          layoutOptions={layoutOptions}
        />
      </div>
    );
  }

  // Mobile condensed view
  if (isMobile && !showMore) {
    return (
      <div className={cx(
        "w-full h-full flex flex-col",
        isDark ? "bg-slate-900 text-white" : "bg-white text-slate-900"
      )}>
        <div className="flex flex-col w-full mx-auto">
          <div className="px-5 pt-5">
            <ScrollThrottlingSelect
              mode="single"
              title={$at("LED State Synchronization")}
              options={ledSyncOptions}
              value={ledSync}
              onChange={handleLedChange}
            />
          </div>

          <div className={cx("h-px mx-5 my-4", isDark ? "bg-slate-700" : "bg-slate-200")} />

          <div className="px-5">
            <ScrollThrottlingSelect
              mode="single"
              title={$at("Keyboard Layout")}
              options={layoutOptions}
              value={safeKeyboardLayout}
              onChange={onKeyboardLayoutChange}
              maxShowCount={maxShowCount}
              specialOptionText="More"
              specialOptionIcon={<LeftSVG />}
              onSpecialOptionClick={() => setShowMore(true)}
            />
          </div>

          <div className={cx("h-px mx-5 my-4", isDark ? "bg-slate-700" : "bg-slate-200")} />

          <div className="px-5 pb-5">
            <ScrollThrottlingSelect
              mode="multiple"
              title={$at("Keys")}
              options={keysOptionsList}
              value={keysOptions}
              onChange={handleShowPressedChange}
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
          "bg-blue-100 dark:bg-blue-900/30"
        )}>
          <LuKeyboard className="w-4 h-4 text-blue-600 dark:text-blue-400" />
        </div>
        <div>
          <div className={cx(
            "text-sm font-semibold",
            isDark ? "text-white" : "text-slate-900"
          )}>
            {$at("Keyboard Settings")}
          </div>
          <div className={cx(
            "text-xs",
            isDark ? "text-slate-400" : "text-slate-500"
          )}>
            {$at("Configure keyboard behavior")}
          </div>
        </div>
      </div>

      {/* Content */}
      <div className="p-4 space-y-5">
        {/* LED Sync */}
        <div>
          <div className="flex items-center gap-2 mb-2">
            <LuKeyboardOff className={cx("w-3.5 h-3.5", isDark ? "text-slate-500" : "text-slate-400")} />
            <div className={cx(
              "text-[11px] font-semibold uppercase tracking-wider",
              isDark ? "text-slate-400" : "text-slate-500"
            )}>
              {$at("LED State Synchronization")}
            </div>
          </div>
          <ScrollThrottlingSelect
            mode="single"
            title=""
            options={ledSyncOptions}
            value={ledSync}
            onChange={handleLedChange}
          />
        </div>

        <div className={cx("h-px -mx-4", isDark ? "bg-slate-700" : "bg-slate-100")} />

        {/* Keyboard Layout */}
        <div>
          <div className="flex items-center gap-2 mb-2">
            <LuKeyboard className={cx("w-3.5 h-3.5", isDark ? "text-slate-500" : "text-slate-400")} />
            <div className={cx(
              "text-[11px] font-semibold uppercase tracking-wider",
              isDark ? "text-slate-400" : "text-slate-500"
            )}>
              {$at("Keyboard Layout")}
            </div>
          </div>
          <ScrollThrottlingSelect
            mode="single"
            title=""
            options={layoutOptions}
            value={safeKeyboardLayout}
            onChange={onKeyboardLayoutChange}
            maxShowCount={maxShowCount}
            specialOptionText="More"
            specialOptionIcon={<LeftSVG />}
            onSpecialOptionClick={() => setShowMore(true)}
          />
        </div>

        <div className={cx("h-px -mx-4", isDark ? "bg-slate-700" : "bg-slate-100")} />

        {/* Keys Display */}
        <div>
          <div className="flex items-center gap-2 mb-2">
            <LuEye className={cx("w-3.5 h-3.5", isDark ? "text-slate-500" : "text-slate-400")} />
            <div className={cx(
              "text-[11px] font-semibold uppercase tracking-wider",
              isDark ? "text-slate-400" : "text-slate-500"
            )}>
              {$at("Keys Display")}
            </div>
          </div>
          <ScrollThrottlingSelect
            mode="multiple"
            title=""
            options={keysOptionsList}
            value={keysOptions}
            onChange={handleShowPressedChange}
          />
        </div>
      </div>

      {/* Keyboard Layout Modal */}
      <KeyboardLayoutModal
        visible={showMore && isDesktop}
        onCancel={() => setShowMore(false)}
        value={safeKeyboardLayout}
        onChange={onKeyboardLayoutChange}
        layoutOptions={layoutOptions}
      />
    </div>
  );
};

export default KeyboardPanel;
