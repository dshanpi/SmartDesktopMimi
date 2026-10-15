# REQ-MIMI-SPLIT-055 移除上层仓库旧后端实现
2026-10-15 实施前记录；关联SPLIT053/054、MIMI-UPDATE-001、MIMI-TRACE-001和DELIVERY033。

## 背景与范围
SDK958已保存并重编private32/public30，054实际系统入口和上层672源检查passed。本轮仅移除ownership明确platform且位于lv_port_linux的23个C实现；接口头文件/冻结共享客户端保留，其他底层服务的最终归属仍后续核对，不宣称完整迁移。原源码与关键增量备份已保留，SDK每个移除源的实际哈希必须匹配。

## 流程和失败分支
选择23个已登记platform C → 逐文件核对SDK public/private原字节和现有上层真实依赖 → 全匹配才移除应用旧副本 → CMake仅上层且显式拒绝混合模式/私有云 → 平台回归读取显式SDK组件的固定配方源码，缺SDK的对应检查明确skip/not_run而不冒充passed → 实际SDK条件运行所有受影响回归和上层重编 → 记录结果。任一源码不匹配停止，失败保留；不回退到隐含旧目录。

## 回归不变量
硬件接口头和V1消息保持原字节；已冻结rc2/rc23不变、不替换可信钥或设备绑定；SDK实现不复制回上层以绕过测试。上层拒绝重新引入已移除实现，即使尚未编译。测试断言保留，平台实现检查实际指向SDK；未配置SDK的检查不计passed，正式系统验收必须提供SDK并执行。只影响构建来源，不升级设备/不上传应用或Release。

## 路径、验收和接续
计划路径：apps/lv_port_linux/CMakeLists.txt及23已登记platform C、docs/update/source-ownership.json、tools/update/source_ownership.py、tests/sdk_source.py、tests/platform/test_host_integration.py、tests/architecture/test_architecture.py、tests/update/test_source_ownership.py，及本门禁/进度/索引/批次。
基准aabc676（完整实际SHA由批次证据记录）。文件迁移核对、主机受影响回归、实际A133上层重编：not_run；新整镜像/物理接口/升级断电：not_run。证据output/backend-source-retirement-20261015。接口头文件及其他服务未拆部分继续明确待办。
