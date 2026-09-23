import React from "react";
import { Popover, PopoverButton, PopoverPanel } from "@headlessui/react";

import { useUiStore } from "@/hooks/stores";
import { selected_bt_bg } from "@/layout/theme_color";

interface PopoverButtonProps {
  buttonText?: string;
  buttonIconNode?: React.ReactNode;
  panelContent: React.ReactNode;
  align?: "left" | "right";
  /** @deprecated - use TopBarPopover for anchor="top" behavior */
  anchor?: "top" | "bottom";
  buttonClassName?: string;
  panelClassName?: string;
}

/**
 * BottomPopoverButton - Opens popover BELOW the button
 * Use for sidebars, bottom bars, or any UI at the bottom of a region
 * Panel will appear below the button with proper spacing
 */
const BottomPopoverButton: React.FC<PopoverButtonProps> = ({
  buttonText,
  buttonIconNode,
  panelContent,
  align = "left",
}) => {
  const setDisableFocusTrap = useUiStore(state => state.setDisableVideoFocusTrap);

  return (
    <Popover className="relative">
      {({ open }) => (
        <>
          <PopoverButton
            as="div"
            className="flex cursor-pointer items-center justify-center"
            onClick={() => setDisableFocusTrap(true)}
          >
            <div
              className={`
                flex items-center justify-center text-xs h-[24px]
                cursor-pointer hover:bg-black/5 dark:hover:bg-white/5
                transition-colors rounded px-2
                ${open ? selected_bt_bg : ""}
              `}
            >
              {buttonIconNode && <span className="flex items-center">{buttonIconNode}</span>}
              {buttonText && (
                <span className="pl-1.5" style={{ position: "relative", top: "1px" }}>
                  {buttonText}
                </span>
              )}
            </div>
          </PopoverButton>

          <PopoverPanel
            anchor="top start"
            className={`
              z-50 w-[260px] overflow-visible
              transition duration-200 ease-out data-closed:scale-95 data-closed:opacity-0
              ${align === "left" ? "origin-top-left" : "origin-top-right"}
            `}
            transition
          >
            <div className="mt-1 overflow-hidden bg-white dark:bg-slate-800">
              {panelContent}
            </div>
          </PopoverPanel>
        </>
      )}
    </Popover>
  );
};

/**
 * TopBarPopover - Opens popover ABOVE the button
 * Use for top bars or any UI at the top of a region
 * Panel will appear above the button with proper spacing
 */
const TopBarPopover: React.FC<PopoverButtonProps> = ({
  buttonText,
  buttonIconNode,
  panelContent,
  align = "left",
}) => {
  const setDisableFocusTrap = useUiStore(state => state.setDisableVideoFocusTrap);

  return (
    <Popover className="relative">
      {({ open }) => (
        <>
          <PopoverButton
            as="div"
            className="flex cursor-pointer items-center justify-center"
            onClick={() => setDisableFocusTrap(true)}
          >
            <div
              className={`
                flex items-center justify-center text-xs h-[24px]
                cursor-pointer hover:bg-black/5 dark:hover:bg-white/5
                transition-colors rounded px-2
                ${open ? selected_bt_bg : ""}
              `}
            >
              {buttonIconNode && <span className="flex items-center">{buttonIconNode}</span>}
              {buttonText && (
                <span className="pl-1.5" style={{ position: "relative", top: "1px" }}>
                  {buttonText}
                </span>
              )}
            </div>
          </PopoverButton>

          <PopoverPanel
            anchor="bottom start"
            className={`
              z-50 w-[260px] overflow-visible
              transition duration-200 ease-out data-closed:scale-95 data-closed:opacity-0
              ${align === "left" ? "origin-bottom-left" : "origin-bottom-right"}
            `}
            transition
          >
            <div className="mb-1 overflow-hidden bg-white dark:bg-slate-800">
              {panelContent}
            </div>
          </PopoverPanel>
        </>
      )}
    </Popover>
  );
};

export default BottomPopoverButton;
export { TopBarPopover };
