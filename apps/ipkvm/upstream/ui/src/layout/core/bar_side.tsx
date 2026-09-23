import { isDesktop, isMobile } from "react-device-detect";
import { AnimatePresence } from "framer-motion";
import { useReactAt } from "i18n-auto-extractor/react";

import { cx } from "@/cva.config";
import StatsSidebar from "@/layout/components_side/Stats/StatsSidebar";
import Clipboard from "@/layout/components_side/Clipboard/Clipboard";
import PowerControl from "@/layout/components_side/Power";
import SharedFolders from "@/layout/components_side/SharedFolders";
import VirtualMediaSource from "@/layout/components_side/VirtualMediaSource";

interface SidebarContainerProps {
  readonly sidebarView: string | null;
}

export default  function SidebarContainer(props: SidebarContainerProps) {
  const { $at } = useReactAt();
  const { sidebarView } = props;

  // Only show sidebar container for actual sidebar views, not for SettingsModal
  const isSidebarView = sidebarView && ["Clipboard", "PowerControl", "SharedFolders", "VirtualMedia"].includes(sidebarView);

  // useConsoleLog()
  // { "border-x-transparent": !sidebarView },
  return (
    <div
      className={cx(
        "flex shrink-0 h-full border-l border-l-slate-800/20 transition-all duration-500 ease-in-out dark:border-l-slate-300/20",
        { "border-x-transparent": !isSidebarView },

      )}
      style={{ width: isSidebarView ? isMobile ? "100%" : "493px" : 0 }}
    >

      <div className={`relative${isMobile ? "w-full" : " w-[493px]"} shrink-0 h-full flex flex-col`}>

        <AnimatePresence>
          <>

          <StatsSidebar title={$at("Clipboard")} targetView={"Clipboard"}>
            <Clipboard />
          </StatsSidebar>
          
          {isDesktop&&<StatsSidebar title={$at("PowerControl")} floatOnMobile={true} targetView={"PowerControl"}>
            <PowerControl />
          </StatsSidebar>}
          <StatsSidebar title={$at("Shared Folders")} targetView={"SharedFolders"}>
            <SharedFolders />
          </StatsSidebar>
          {isDesktop&&<StatsSidebar title={$at("Virtual Media Source")} targetView={"VirtualMedia"}>
            <VirtualMediaSource />
          </StatsSidebar> }
          </>
        </AnimatePresence>


       </div>
    </div>
  );
}
