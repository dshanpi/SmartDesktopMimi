# HDMI 平台服务归属
REQ-MIMI-SPLIT-057：A133采集、显示和音频实现已移至私有SDK标准overlay组件：
`openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-hdmi`。
先按私有SDK的scripts/apply_overlay.sh应用；系统构建由scripts/build_a133_hdmi.py验证固定来源/依赖并独立编译。
此目录保留归属说明，不再提供上层实现或旧源回退。程序/协议保持，未宣称本轮真机通过。
