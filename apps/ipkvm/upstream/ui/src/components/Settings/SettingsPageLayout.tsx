import React from "react";
import { cx } from "@/cva.config";

interface SettingsPageLayoutProps {
  children: React.ReactNode;
  className?: string;
}

export function SettingsPageLayout({
  children,
  className,
}: SettingsPageLayoutProps) {
  return (
    <div className={cx("w-full space-y-4", className)}>
      {children}
    </div>
  );
}

export default SettingsPageLayout;
