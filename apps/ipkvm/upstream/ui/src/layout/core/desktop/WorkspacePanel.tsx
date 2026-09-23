import { AppWindow, Bot, X } from "lucide-react";

import { cx } from "@/cva.config";
import { useUiStore } from "@/hooks/stores";
import AgentContent from "@/layout/components_setting/agent/AgentContent";
import ApplicationContent from "@/layout/components_setting/apps/ApplicationContent";

export default function WorkspacePanel() {
  const sidebarView = useUiStore(state => state.sidebarView);
  const setSidebarView = useUiStore(state => state.setSidebarView);
  const setDisableFocusTrap = useUiStore(state => state.setDisableVideoFocusTrap);

  if (sidebarView !== "AgentWorkspace" && sidebarView !== "ApplicationsWorkspace") {
    return null;
  }

  const isAgent = sidebarView === "AgentWorkspace";
  const close = () => {
    setSidebarView(null);
    setDisableFocusTrap(false);
  };

  return (
    <aside
      className={cx(
        "relative z-30 flex h-full w-[min(46vw,680px)] min-w-[420px] shrink-0 flex-col",
        "border-l border-slate-200 bg-slate-50 shadow-[-18px_0_45px_-35px_rgba(15,23,42,.65)]",
        "dark:border-slate-800 dark:bg-[#0b111b]",
      )}
      data-testid={isAgent ? "agent-workspace" : "applications-workspace"}
      onMouseDown={event => event.stopPropagation()}
      onMouseUp={event => event.stopPropagation()}
      onKeyDown={event => event.stopPropagation()}
      onKeyUp={event => event.stopPropagation()}
    >
      <header className="flex h-14 shrink-0 items-center justify-between border-b border-slate-200 bg-white px-5 dark:border-slate-800 dark:bg-slate-950">
        <div className="flex items-center gap-3">
          <span className={cx(
            "flex size-8 items-center justify-center rounded-xl",
            isAgent
              ? "bg-violet-100 text-violet-700 dark:bg-violet-950 dark:text-violet-300"
              : "bg-blue-100 text-blue-700 dark:bg-blue-950 dark:text-blue-300",
          )}>
            {isAgent ? <Bot className="size-4" /> : <AppWindow className="size-4" />}
          </span>
          <div>
            <h2 className="text-sm font-semibold text-slate-900 dark:text-white">
              {isAgent ? "AI HDMI MCP" : "Applications"}
            </h2>
            <p className="text-[11px] text-slate-500 dark:text-slate-400">
              {isAgent ? "Conversation and HID execution flow" : "Install and manage A133 user applications"}
            </p>
          </div>
        </div>
        <button
          type="button"
          onClick={close}
          className="rounded-lg p-2 text-slate-500 transition-colors hover:bg-slate-100 hover:text-slate-900 dark:hover:bg-slate-800 dark:hover:text-white"
          aria-label="Close workspace"
        >
          <X className="size-4" />
        </button>
      </header>
      <div className="min-h-0 flex-1 overflow-y-auto p-5">
        {isAgent ? <AgentContent /> : <ApplicationContent />}
      </div>
    </aside>
  );
}
