# REQ-MIMI-SPLIT-057 HDMI 平台实现迁入 SDK
2026-10-15 实施前记录。关联SPLIT053—056、DELIVERY033、MIMI-UPDATE-001和MIMI-TRACE-001。

## 背景、范围与流程
apps/hdmi_preview仍把A133 V4L2、显示及音频平台实现放在上层应用仓库。迁移现有实现与原许可证至私有SDK标准overlay的smartdesktopmimi-a133-hdmi组件；应用系统构建入口消费固定组件，不改运行行为、设备路径或协议。先用原源新目录编译建立字节参考，记录实际C++头/工具链/库/配置输入；SDK自含编译与参考匹配、标准overlay重放及敏感内容检查通过后，按已有私有SDK例外推送并独立读回；随后删除上层实现、切换系统入口和显式SDK测试，执行受影响验证。源或依赖变化/闭包逃逸/目标不匹配时拒绝，不回退旧源、不删除有效产物，保留失败。

## 回归不变量
HDMI默认关闭，主机编译不打开采集、不操作板卡。原C++代码逐字节保留，ELF/ABI与原参考对照；接口、签名钥、绑定及持久数据不变。构建器固定实际C++逻辑编译器名（SDK wrapper依赖argv0），不执行解析后的wrapper名；预检在清理/补依赖前执行。SDK组件源闭包不带私钥、设备凭据或第三方库体；许可证保留，标准overlay唯一入口不变。只安装新组件到开发SDK，不覆盖已审核系统输入。旧rc2/rc23候选冻结，文档不重建；不上传应用/Release、不宣称T527/RK3576或真机支持。

## 实现位置与验收
计划：scripts/build_a133_hdmi.py、scripts/build_apps.sh、apps/hdmi_preview；tests/sdk_source.py、平台测试及专用入口/依赖边界测试；docs/update/source-ownership.json；SDK overlay/meta及README。起点应用45871ca、SDKd9eca3dcebd3f4e869161699661f3db980d644da，实际完整提交/锁/产物SHA绑定output/hdmi-source-split-20261015。全流程源实现/编译/上传读回/上层切换not_run，真实设备/完整新镜像not_run，migration_complete=false。

## 接续
先固定原编译参考和实际依赖，再按上述步骤执行。剩余ipkvm、Tuya、混合业务、接口头/冻结客户端继续另项；ADB绑定/Xorg就绪后优先恢复必需真机验收。记录不替代验收，不把原产物锁改写为新源码。
