export const site = 'https://dshanpi.github.io';
export const base = '/SmartDesktopMimi';
export const repository = 'https://github.com/dshanpi/SmartDesktopMimi';
export const routes = [
  {
    "source": "docs/tutorial/index.md",
    "slug": "index",
    "title": "SmartDesktopMimi 使用与开发指南",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/hardware.md",
    "slug": "hardware",
    "title": "准备设备并连接硬件",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/install.md",
    "slug": "install",
    "title": "获取镜像并完成安装前检查",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/quickstart.md",
    "slug": "quickstart",
    "title": "完成首次启动与基本检查",
    "group": "开始使用"
  },
  {
    "source": "docs/tutorial/desktop.md",
    "slug": "desktop",
    "title": "操作桌面、应用与设置",
    "group": "日常操作"
  },
  {
    "source": "docs/tutorial/network.md",
    "slug": "network",
    "title": "连接 Wi-Fi 并检查网络",
    "group": "日常操作"
  },
  {
    "source": "docs/tutorial/bluetooth.md",
    "slug": "bluetooth",
    "title": "连接手机并播放蓝牙音频",
    "group": "日常操作"
  },
  {
    "source": "docs/tutorial/settings-data.md",
    "slug": "settings-data",
    "title": "记录设置并备份持久数据",
    "group": "日常操作"
  },
  {
    "source": "docs/tutorial/ai-iot.md",
    "slug": "ai-iot",
    "title": "使用涂鸦助手与自由对话",
    "group": "联网与远程控制"
  },
  {
    "source": "docs/tutorial/pairing.md",
    "slug": "pairing",
    "title": "查询绑定状态并完成账号配对",
    "group": "联网与远程控制"
  },
  {
    "source": "docs/tutorial/iot.md",
    "slug": "iot",
    "title": "授权智家服务并控制设备",
    "group": "联网与远程控制"
  },
  {
    "source": "docs/tutorial/ipkvm.md",
    "slug": "ipkvm",
    "title": "通过浏览器查看并控制电脑",
    "group": "联网与远程控制"
  },
  {
    "source": "docs/tutorial/mcp.md",
    "slug": "mcp",
    "title": "配置 AI 控制并接入 MCP",
    "group": "联网与远程控制"
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
    "title": "理解系统分层与启动顺序",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/source-map.md",
    "slug": "source-map",
    "title": "选择源码仓库并开展平台适配",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/user-app.md",
    "slug": "user-app",
    "title": "制作、签名并安装第一个 APP",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/native-app.md",
    "slug": "native-app",
    "title": "为 APP 添加 C 逻辑与持久数据",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/build-app.md",
    "slug": "build-app",
    "title": "构建独立桌面应用",
    "group": "应用开发"
  },
  {
    "source": "docs/tutorial/build-system.md",
    "slug": "build-system",
    "title": "构建并核验 A133 系统镜像",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/ota.md",
    "slug": "ota",
    "title": "检查并执行应用或系统更新",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/release.md",
    "slug": "release",
    "title": "准备产品发布资料并复核交付",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/troubleshooting.md",
    "slug": "troubleshooting",
    "title": "按症状定位并记录故障",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/validation.md",
    "slug": "validation",
    "title": "制定检查清单并记录验收结果",
    "group": "系统维护"
  },
  {
    "source": "docs/tutorial/version-status.md",
    "slug": "version-status",
    "title": "核对版本与教程适用范围",
    "group": "参考资料"
  },
  {
    "source": "docs/tutorial/gallery.md",
    "slug": "gallery",
    "title": "对照界面示例与截图来源",
    "group": "参考资料"
  },
  {
    "source": "docs/tutorial/glossary.md",
    "slug": "glossary",
    "title": "查询术语、文件与操作位置",
    "group": "参考资料"
  },
  {
    "source": "docs/tutorial/site.md",
    "slug": "site",
    "title": "维护并发布 GitHub Pages 教程",
    "group": "参考资料"
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
