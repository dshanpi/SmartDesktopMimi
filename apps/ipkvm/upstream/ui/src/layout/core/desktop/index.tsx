import { isMobile } from "react-device-detect";

import MobileDesktop from "@/layout/core/desktop/DesktopMobile";
import PCDesktop from "@/layout/core/desktop/DesktopPC";
import { useTerminal } from "@/layout/components_bottom/terminal/useTerminal";

export default function Desktop() {
  useTerminal();
  return isMobile ? <MobileDesktop /> : <PCDesktop />;
}