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

## 实现与初始检查
23个已登记platform C已逐文件核对SDK配方与实际源码原字节后移除（retirement-before.json）；接口头文件未动。CMake默认且强制上层，旧backend-only块移除，拒绝混合构建/私有云；SDK后端仍走054入口。ownership保留退休清单和原SHA，重新引入即拒绝，即使未参与编译。
平台实现测试通过tests/sdk_source.py读取显式MIMI_A133_PROVIDER_ROOT和固定配方/实际源SHA；未配置时只对应实现检查SkipTest/not_run，显式配置错误则失败；没有旧源码回退。28边界/应用构建收据回归passed；现有672编译源归属复核passed。架构+平台集成回归正在运行，实际新源码重编not_run；结果后续追加。

## 最终受影响验证（源码02797a4ea15a662992a14ce920eda56b66b18414）
实际SDK958配置下33架构+73平台集成共106项全部passed、0skip；28边界/应用构建收据回归passed，总134项本批主机检查。平台实现检查读取固定SDK源，而不是恢复已删除副本；显式无SDK的单项另行记录skip/not_run，不计134通过。显式混合CMake构建返回非0且明确Backend moved to SDK，负向拒绝passed。
移除23个C实现后实际上层重编exit0/源码clean/672源归属passed；ELF4878416B/hash4891196a5e3134ddda967f2823ffe54111d8f97e3aa2588272d4e17c4e673045，与054清理前字节相同。24个platform接口头对照aabc676全部原字节不变；SDK private/public预检仍passed。verified-result.json绑定实际代码SHA；不借用054或旧厂包的实机验收。
完整build_apps正向归集/全系统重建、新设备升级/回退/断电/数据仍not_run。接口头/冻结共享客户端和apps/platform_services、hdmi_preview、ipkvm等其他系统源归属尚待最终收尾，migration_complete=false；不是全部拆仓完成。原rc2镜像/rc23签名包及物理rc17未改，无应用/Release上传。下一步优先恢复已登记设备ADB scope后可信OTA/失联对照，或继续其余源码归属；不把已授权但未执行项目标passed。

054完整应用构建接续已通过（保留npm首轮失败），实际SDK后端归集与其他系统组件衔接通过；SOURCE-OWNERSHIP/README同步当前来源，旧接口和其余源码待办保持。详见output/product-pipeline-20261015/verified-result.json；未新增物理通过。
