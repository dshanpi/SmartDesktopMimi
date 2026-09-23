import { isMobile } from 'react-device-detect';

import MobileTopBar from "@/layout/core/bar_top/TopBarMobile";
import TopBarPC from "@/layout/core/bar_top/TopBarPC";

export default function Index() {
  if(isMobile){
    return <MobileTopBar />;
  }

  return (
    <TopBarPC />
  );
}
