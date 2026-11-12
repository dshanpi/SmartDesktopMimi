export const site = 'https://dshanpi.github.io';
export const base = '/SmartDesktopMimi';
export const repository = 'https://github.com/dshanpi/SmartDesktopMimi';
export const routes = [
  {
    "source": "docs/tutorial/index.md",
    "slug": "index",
    "title": "认识智能桌面小咪",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/version-status.md",
    "slug": "version-status",
    "title": "版本与已知限制",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/hardware.md",
    "slug": "hardware",
    "title": "硬件准备与连接",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/install.md",
    "slug": "install",
    "title": "固件获取与安装前检查",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/quickstart.md",
    "slug": "quickstart",
    "title": "完成首次启动",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/gallery.md",
    "slug": "gallery",
    "title": "界面截图与证据来源",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/desktop.md",
    "slug": "desktop",
    "title": "操作桌面与内置应用",
    "group": "使用设备"
  },
  {
    "source": "docs/tutorial/network.md",
    "slug": "network",
    "title": "Wi-Fi、蓝牙与音频",
    "group": "使用设备"
  },
  {
    "source": "docs/tutorial/ai-iot.md",
    "slug": "ai-iot",
    "title": "涂鸦、IoT 与设备配对",
    "group": "使用设备"
  },
  {
    "source": "docs/tutorial/ipkvm.md",
    "slug": "ipkvm",
    "title": "浏览器 IPKVM 完整上手",
    "group": "使用设备"
  },
  {
    "source": "docs/tutorial/mcp.md",
    "slug": "mcp",
    "title": "AI 电脑控制与 MCP",
    "group": "使用设备"
  },
  {
    "source": "docs/tutorial/settings-data.md",
    "slug": "settings-data",
    "title": "设置、持久数据与备份",
    "group": "使用设备"
  },
  {
    "source": "docs/tutorial/environment.md",
    "slug": "environment",
    "title": "准备 Ubuntu 开发环境",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/architecture.md",
    "slug": "architecture",
    "title": "理解架构与启动链路",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/user-app.md",
    "slug": "user-app",
    "title": "实战：制作并安装用户 APP",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/build-app.md",
    "slug": "build-app",
    "title": "构建独立桌面应用",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/source-map.md",
    "slug": "source-map",
    "title": "源码归属与平台移植",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/build-system.md",
    "slug": "build-system",
    "title": "构建 A133 系统镜像",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/ota.md",
    "slug": "ota",
    "title": "桌面更新与系统 A/B OTA",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/release.md",
    "slug": "release",
    "title": "产品发布与验收资料",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/troubleshooting.md",
    "slug": "troubleshooting",
    "title": "按症状定位故障",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/validation.md",
    "slug": "validation",
    "title": "验证方法与完成标准",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/site.md",
    "slug": "site",
    "title": "维护与发布文档站",
    "group": "文档维护"
  }
];
export const assets = [
  {
    "source": "docs/debug-evidence/a133-en-us-20260906/final/desktop-r3.png",
    "file": "desktop.png"
  },
  {
    "source": "docs/debug-evidence/a133-en-us-20260906/final/all-apps.png",
    "file": "all-apps.png"
  },
  {
    "source": "docs/debug-evidence/a133-en-us-20260906/final/ai-chat.png",
    "file": "ai-chat.png"
  },
  {
    "source": "docs/debug-evidence/a133-en-us-20260906/bluetooth.png",
    "file": "bluetooth.png"
  },
  {
    "source": "docs/debug-evidence/a133-en-us-20260906/system-update.png",
    "file": "system-update.png"
  }
];
export const sidebar = [...new Set(routes.map(p => p.group))].map(label => ({label, items: routes.filter(p => p.group === label).map(p => ({label: p.title, slug: p.slug}))}));
