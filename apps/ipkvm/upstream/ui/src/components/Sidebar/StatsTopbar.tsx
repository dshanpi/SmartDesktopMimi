import { useReactAt } from "i18n-auto-extractor/react";

import { useUiStore } from "@/hooks/stores";
import {  dark_font_style } from "@/layout/theme_color";

interface StatsSidebarProps {
  title: string;
  targetView: string;
  children: React.ReactNode;
  className?: string;
}

import { theme as AntTheme } from "antd";

import SlideAnimation from "@components/Sidebar/SlideAnimation";

const StatsTobbar = ({ title, targetView, children, className = "" }: StatsSidebarProps) => {
  const token = AntTheme.useToken();
  const sidebarView = useUiStore(state => state.sidebarView);
  const topBarView = useUiStore(state => state.topBarView);

  const { $at } = useReactAt();

  return(

    <SlideAnimation
      direction={"up"}
      isVisible={sidebarView === targetView || topBarView === targetView}
    >
      <div
        className={`w-full h-full border-b-1 border-b-[rgb(229,229,229)] dark:border-b-[rgb(56,56,56)] ${className}`}
        style={{ backgroundColor: token.token.colorBgContainer }}
      >

        <div className="grid grid-rows-[auto_1fr] h-full overflow-hidden">
          {title !== "" &&
            <div
              className={`${dark_font_style} font-microsoft-yahei-ui text-lg font-bold leading-6 py-5 px-[20px]`}>{$at(title)}</div>}
          <div className="overflow-y-auto">
            <div className="px-[20px]">
              {children}
            </div>
          </div>
        </div>
      </div>
    </SlideAnimation>
  )
};
export default StatsTobbar;