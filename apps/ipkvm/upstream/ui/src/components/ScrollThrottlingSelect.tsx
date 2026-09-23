import React from 'react';
import { CheckOutlined } from '@ant-design/icons';
import { isMobile } from "react-device-detect";
import { cx } from "@/cva.config";
import { useThemeSettings } from "@routes/login_page/useLocalAuth";

export interface Option {
  label: string;
  value: string;
}

interface ScrollThrottlingSelectProps {
  mode?: 'single' | 'multiple';
  value?: string | string[];
  onChange?: (value: string | string[]) => void;
  options?: Option[];
  title?: string;
  disabled?: boolean;
  specialOptionText?: string;
  specialOptionIcon?: React.ReactNode;
  onSpecialOptionClick?: () => void;
  maxShowCount?: number;
}

const defaultOptions: Option[] = [
  { label: 'off', value: 'off' },
];

const ScrollThrottlingSelect: React.FC<ScrollThrottlingSelectProps> = ({
  mode = 'single',
  value,
  onChange,
  options = defaultOptions,
  title = 'Scroll Throttling',
  disabled = false,
  specialOptionText,
  specialOptionIcon,
  onSpecialOptionClick,
  maxShowCount
}) => {
  const { isDark } = useThemeSettings();

  const handleSingleSelect = (selectedValue: string) => {
    if (disabled) return;
    onChange?.(selectedValue);
  };

  const handleMultipleSelect = (selectedValue: string) => {
    if (disabled) return;
    const currentValues = Array.isArray(value) ? value : [];
    const newValues = currentValues.includes(selectedValue)
      ? currentValues.filter(v => v !== selectedValue)
      : [...currentValues, selectedValue];
    onChange?.(newValues);
  };

  const isSelected = (optionValue: string): boolean => {
    if (mode === 'single') {
      return value === optionValue;
    } else {
      return Array.isArray(value) && value.includes(optionValue);
    }
  };

  const handleSpecialOptionClick = () => {
    if (disabled) return;
    onSpecialOptionClick?.();
  };

  const getVisibleOptions = () => {
    if (!maxShowCount || maxShowCount >= options.length) {
      return {
        visibleOptions: options,
        hiddenCount: 0
      };
    }
    return {
      visibleOptions: options.slice(0, maxShowCount),
      hiddenCount: options.length - maxShowCount
    };
  };

  const { visibleOptions } = getVisibleOptions();

  if (isMobile) {
    return (
      <div className="w-full">
        {/* Section Title */}
        <div className={cx(
          "text-xs font-semibold uppercase tracking-wider mb-3",
          isDark ? "text-slate-400" : "text-slate-500"
        )}>
          {title}
        </div>

        {/* Options List */}
        <div className="flex flex-col rounded-lg overflow-hidden divide-y divide-slate-200 dark:divide-slate-700">
          {visibleOptions.map(option => (
            <button
              key={option.value}
              type="button"
              onClick={() => mode === 'single'
                ? handleSingleSelect(option.value)
                : handleMultipleSelect(option.value)
              }
              disabled={disabled}
              className={cx(
                "flex items-center justify-between py-3.5 px-4 w-full text-left",
                "transition-colors duration-150",
                "hover:bg-slate-100 dark:hover:bg-slate-800",
                isSelected(option.value)
                  ? "bg-emerald-50 dark:bg-emerald-900/20"
                  : "",
                disabled && "opacity-50 cursor-not-allowed"
              )}
            >
              <span className={cx(
                "text-sm font-medium",
                isDark ? "text-white" : "text-slate-900"
              )}>
                {option.label}
              </span>
              <span className={cx(
                "text-emerald-600 dark:text-emerald-400",
                "transition-opacity duration-150",
                isSelected(option.value) ? "opacity-100" : "opacity-0"
              )}>
                <CheckOutlined />
              </span>
            </button>
          ))}

          {specialOptionText && (
            <button
              key="special-option"
              type="button"
              onClick={handleSpecialOptionClick}
              disabled={disabled}
              className={cx(
                "flex items-center justify-between py-3.5 px-4 w-full text-left",
                "transition-colors duration-150",
                "hover:bg-slate-100 dark:hover:bg-slate-800",
                disabled && "opacity-50 cursor-not-allowed"
              )}
            >
              <span className={cx(
                "text-sm font-medium",
                isDark ? "text-white" : "text-slate-900"
              )}>
                {specialOptionText}
              </span>
              <span className={cx(isDark ? "text-slate-400" : "text-slate-500")}>
                {specialOptionIcon}
              </span>
            </button>
          )}
        </div>
      </div>
    );
  }

  // Desktop version
  return (
    <div className="w-full">
      {/* Section Title */}
      <div className={cx(
        "text-[11px] font-semibold uppercase tracking-wider mb-2 px-1",
        isDark ? "text-slate-400" : "text-slate-500"
      )}>
        {title}
      </div>

      {/* Options List */}
      <div className="flex flex-col rounded-lg overflow-hidden">
        {visibleOptions.map(option => (
          <button
            key={option.value}
            type="button"
            onClick={() => mode === 'single'
              ? handleSingleSelect(option.value)
              : handleMultipleSelect(option.value)
            }
            disabled={disabled}
            className={cx(
              "group flex items-center justify-between px-3 py-2.5 w-full text-left",
              "rounded-md transition-all duration-150",
              "hover:bg-slate-100 dark:hover:bg-slate-700/50",
              isSelected(option.value) && "bg-emerald-50 dark:bg-emerald-900/20",
              disabled && "opacity-50 cursor-not-allowed"
            )}
          >
            <span className={cx(
              "text-xs font-medium transition-colors",
              isSelected(option.value)
                ? "text-emerald-700 dark:text-emerald-300"
                : isDark ? "text-slate-200" : "text-slate-700"
            )}>
              {option.label}
            </span>
            <span className={cx(
              "transition-all duration-150",
              isSelected(option.value)
                ? "text-emerald-600 dark:text-emerald-400 scale-100 opacity-100"
                : "scale-75 opacity-0"
            )}>
              <CheckOutlined />
            </span>
          </button>
        ))}

        {specialOptionText && (
          <button
            key="special-option"
            type="button"
            onClick={handleSpecialOptionClick}
            disabled={disabled}
            className={cx(
              "group flex items-center justify-between px-3 py-2.5 w-full text-left",
              "rounded-md mt-1 transition-all duration-150",
              "hover:bg-slate-100 dark:hover:bg-slate-700/50",
              disabled && "opacity-50 cursor-not-allowed"
            )}
          >
            <span className={cx(
              "text-xs font-medium",
              isDark ? "text-slate-200" : "text-slate-700"
            )}>
              {specialOptionText}
            </span>
            <span className={cx(isDark ? "text-slate-400" : "text-slate-500")}>
              {specialOptionIcon}
            </span>
          </button>
        )}
      </div>
    </div>
  );
};

export default ScrollThrottlingSelect;
