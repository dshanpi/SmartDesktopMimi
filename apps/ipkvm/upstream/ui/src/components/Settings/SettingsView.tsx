import React from "react";
import { isMobile } from "react-device-detect";
import { cx } from "@/cva.config";
import { useThemeSettings } from "@routes/login_page/useLocalAuth";

interface SettingsItemProps {
  readonly title: string;
  readonly description: string | React.ReactNode;
  readonly badge?: string;
  readonly className?: string;
  readonly loading?: boolean;
  readonly children?: React.ReactNode;
  readonly noCol?: boolean;
}

export function SettingsItem(props: SettingsItemProps) {
  const { title, description, badge, children, noCol = false } = props;
  const { isDark } = useThemeSettings();

  return (
    <div
      className={cx(
        "flex items-start justify-between gap-6 rounded-xl p-4",
        "bg-slate-50 dark:bg-slate-800/50",
        "border border-slate-200 dark:border-slate-700",
        "transition-colors duration-150",
        !noCol && isMobile ? "flex-col w-full" : ""
      )}
    >
      {/* Left side - Title and Description */}
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2">
          <h3 className={cx(
            "text-sm font-semibold",
            isDark ? "text-white" : "text-slate-900"
          )}>
            {title}
          </h3>
          {badge && (
            <span className={cx(
              "inline-flex items-center rounded-full px-2 py-0.5 text-[10px] font-medium",
              "bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400"
            )}>
              {badge}
            </span>
          )}
        </div>
        <p className={cx(
          "mt-1 text-sm",
          isDark ? "text-slate-400" : "text-slate-500"
        )}>
          {description}
        </p>
      </div>

      {/* Right side - Control */}
      {children && (
        <div className={cx(
          "flex shrink-0 items-center",
          noCol && isMobile ? "w-full mt-3" : ""
        )}>
          {children}
        </div>
      )}
    </div>
  );
}

export function SettingsItemNew(props: SettingsItemProps) {
  const { title, description, badge, children, className } = props;
  const { isDark } = useThemeSettings();

  return (
    <div
      className={cx(
        "flex items-start justify-between gap-6 rounded-xl p-4",
        "bg-slate-50 dark:bg-slate-800/50",
        "border border-slate-200 dark:border-slate-700",
        className
      )}
    >
      {/* Left side - Title and Description */}
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2">
          <h3 className={cx(
            "text-sm font-semibold",
            isDark ? "text-white" : "text-slate-900"
          )}>
            {title}
          </h3>
          {badge && (
            <span className={cx(
              "inline-flex items-center rounded-full px-2 py-0.5 text-[10px] font-medium",
              "bg-red-100 text-red-700 dark:bg-red-900/30 dark:text-red-400"
            )}>
              {badge}
            </span>
          )}
        </div>
        <p className={cx(
          "mt-1 text-sm",
          isDark ? "text-slate-400" : "text-slate-500"
        )}>
          {description}
        </p>
      </div>

      {/* Right side - Control */}
      {children && (
        <div className="flex shrink-0 items-center">
          {children}
        </div>
      )}
    </div>
  );
}

// Section wrapper component for grouping related settings
export function SettingsSection({
  title,
  description,
  children,
  className,
}: {
  title?: string;
  description?: string;
  children: React.ReactNode;
  className?: string;
}) {
  const { isDark } = useThemeSettings();

  return (
    <div className={cx("space-y-4", className)}>
      {title && (
        <div className="space-y-1">
          <h2 className={cx(
            "text-base font-semibold",
            isDark ? "text-white" : "text-slate-900"
          )}>
            {title}
          </h2>
          {description && (
            <p className={cx(
              "text-sm",
              isDark ? "text-slate-400" : "text-slate-500"
            )}>
              {description}
            </p>
          )}
        </div>
      )}
      <div className="space-y-3">
        {children}
      </div>
    </div>
  );
}
