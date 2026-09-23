import { useState , useEffect } from "react";
import { Select } from "antd";
import {useReactAt} from 'i18n-auto-extractor/react'
import { isMobile } from "react-device-detect";

import { useJsonRpc } from "@/hooks/useJsonRpc";
import { SettingsPageHeader } from "@components/Settings/SettingsPageheader";
import { SettingsPageLayout } from "@components/Settings/SettingsPageLayout";
import notifications from "@/notifications";
import { useSettingsStore } from "@/hooks/stores";
import { SettingsItem } from "@components/Settings/SettingsView";
import enJSON from '@/locales/en.json';
import zhJSON from '@/locales/zh.json';
import { ThemeMode, useTheme } from "@/layout/contexts/ThemeContext";
import { dark_font_style } from "@/layout/theme_color";

const { Option } = Select;


export default function SettingsGeneral() {
  const [send] = useJsonRpc();
  const [autoUpdate, setAutoUpdate] = useState(true);
  const { $at, setCurrentLang } = useReactAt();

  // Theme and Language State
  const [theme, setTheme] = useState<string>('light');
  const { setThemeMode } = useTheme();
  const language = useSettingsStore(state => state.language);
  const setLanguage = useSettingsStore(state => state.setLanguage);

  useEffect(() => {
    send("getAutoUpdateState", {}, resp => {
      if ("error" in resp) return;
      setAutoUpdate(resp.result as boolean);
    });
  }, [send]);

  // Language Change Handler
  const handleLanguageChange = (value: string) => {
    setLanguage(value);
    setCurrentLang(value, value === 'en' ? enJSON : zhJSON);
  };

  // Initialize Language
  useEffect(() => {
    setCurrentLang(language, language === 'en' ? enJSON : zhJSON);
  }, [language, setCurrentLang]);

  // Theme Change Handler
  const handleThemeChange = (value: string) => {
    const root = document.documentElement;
    setThemeMode(value as ThemeMode)
    
    localStorage.setItem('theme', value);
    root.classList.remove('light', 'dark');
    root.classList.add(value);
    

    setTheme(value);
  };

  // Initialize Theme
  useEffect(() => {
    const savedTheme = localStorage.getItem('theme') || 'light';
    setTheme(savedTheme);

    const root = document.documentElement;
    
    root.classList.remove('light', 'dark');
    root.classList.add(savedTheme);
     
  }, [theme]);

  return (
    <SettingsPageLayout>
      <SettingsPageHeader
        title={$at("General")}
        description={$at("Configure device settings and update preferences")}
      />
      <div className="space-y-4">
        <SettingsItem
          title={$at("Theme")}
          description={$at("Choose your preferred color theme")}
        >
          <Select
            value={theme}
            onChange={handleThemeChange}
            className="!w-full !h-[36px] min-w-[120px]"
          >
            <Option value="light" className={dark_font_style}>{$at('Light')}</Option>
            <Option value="dark" className={dark_font_style}>{$at('Dark')}</Option>
          </Select>
        </SettingsItem>
      </div>

      <div className="space-y-4">
        <SettingsItem
          title={$at("Language")}
          description={$at("Choose your language")}
        >
          <Select
            value={language}
            onChange={handleLanguageChange}
            className="!w-full !h-[36px] min-w-[120px]"
          >
            <Option value="en" className={dark_font_style}>{$at('English')}</Option>
            <Option value="zh" className={dark_font_style}>{$at('中文')}</Option>
          </Select>
        </SettingsItem>
      </div>
    </SettingsPageLayout>
  );
}
