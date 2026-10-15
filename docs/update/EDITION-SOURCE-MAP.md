# 公共桌面与私有配对：实际源码归属清单
REQ-MIMI-EDITION-062；这是当前源码梳理，**两产品构建隔离尚未完成**，不将当前历史快照称为已去掉专有接入的开源版。

| 实际源码/输入 | 目标归属 | 动作与当前状态 |
| --- | --- | --- |
| apps/lv_port_linux/src/ui、普通应用、字体/Web资源 | 公共共享 | 单一源码，两类设备共用公共签名应用更新，不复制桌面 |
| services/mimi-update、tools/update、构建/deb工具 | 公共共享 | 应用更新不得覆盖平台/私有接入/身份/凭据；硬件匹配不变 |
| apps/lv_port_linux/src/apps/iot、services/mimi-iot-bridge | 公共共享 | 通用云项目的列表/控制与设备自身一机一码授权分层，真实云资源仍未提供 |
| apps/lv_port_linux/src/apps/voice和通用对话界面 | 公共共享 | 可选语音文字控制继续公开，不把普通AI交互判为私有 |
| SDK private/code/.../service_cloud.c、service_cloud_validation.c及外置100ask输入 | 专有适配转SmartDesktopMimi-Private | 当前仍是冻结SDK private配方，下一步抽取注册/刷新/自动绑定产品接入；SDK保留契约/硬件实现，不能只删密钥就算公共隔离 |
| scripts/factory_provision_device.sh、scripts/provision_tuya_license.sh、tools/factory-assistant | 产品私有生产工具候选 | 连同配置/模板依赖迁移并独立验证；真实xlsx/凭据库/授权输入不进任何Git。尚未迁移 |
| third_party/TuyaOpen/apps/tuya.ai/your_chat_bot/src/tuya_main.c和app_chat_bot.c | 混合源按行为拆分 | 通用对话/上游许可保留；授权读取、一机一码初始化和生产接入归私有。不能把整个上游TuyaOpen或普通AI界面复制成私有版。尚未抽取 |
| A133采集/显示、后端、五平台兼容服务和板级适配 | 既有私有SDK标准overlay | 两产品通过接口消费；板级SDK与产品专有配对归属分别登记 |

## 必需验证与接续
公共构建不编译/注册两项专有接入，不能因遗留凭据自动启用或访问注册服务器；私有构建必须显式提供固定版本适配，缺失拒绝。共享公共更新包需分别验证公共/私有平台接口以及升级后私有组件、绑定、数据保留；未做仍not_run。

新镜像对外名SmartDesktopMimi-SystemImage-v<版本>.img.gz；内部硬件身份/系统锁保留。已上传rc2/rc23不覆盖改名或回填新来源，新名称生成/发布链已由IMAGE063实现并通过主机检查；真实新SDK镜像未构建。

逐文件SHA/起点见开发主机output/edition-split-20261015/source-boundary-inventory.json，不含源正文或秘密。目标私有仓库已只读确认private/空仓库/有push权限，尚未初始化或复制内容。先固定专有入口与构建依赖，再分批迁移和测试。
