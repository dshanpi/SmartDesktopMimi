import { cx } from "@/cva.config";

export default function InfoBar() {
  return (
    <div className="border-t border-t-slate-800/30 bg-white text-slate-800 dark:border-t-slate-300/20 dark:bg-slate-900 dark:text-slate-300">
      <div className="flex flex-wrap items-stretch justify-between gap-1">
        <div className="flex items-center">
          <div className="flex flex-wrap items-center gap-x-4 pl-2">
            {/* Left side - reserved for debug info */}
          </div>
        </div>

        <div className="first:divide-l flex items-center divide-x divide-slate-800/20 dark:divide-slate-300/20">
          {/* Right side - reserved for status indicators */}
        </div>
      </div>
    </div>
  );
}
