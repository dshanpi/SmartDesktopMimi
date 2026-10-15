# REQ-MIMI-CI-060 首次上传后发布配置与UI清单收尾
2026-10-15 先记录再修改；关联DELIVERY058、TRACE059、UI018与MIMI-TRACE-001。

## 背景、流程与失败
远端实际application-publish工作流无job且配置失败；本地YAML解析证实第6行description中的未引号冒号非法。远端application-update的RSA/事务测试已通过，UI coverage阶段因清单仍是旧8应用/旧按钮失败；不假称CI全绿。修复仅给description正确加引号；逐项审阅11个变化的交互源（3新IoT/配对/语音入口、设置/首页分页以及原处理体变化），补足已批准功能的场景并重新记录现有UI哈希/回调。不是新UI设计或运行逻辑修改。

## 不变量与实现
不弱化UI changed失败、不删除测试或空录通过；新场景检查单保持not_run；物理语音/涂鸦等仍未验收。不改已冻结镜像/应用/签名/标签。发布工作流仍手动dispatch、受控runner和门禁，不因修语法自动发布。实现位置.github/workflows/application-publish.yml、tools/update/ui_catalog.py、tests/update/ui-catalog.json、对应回归与门禁批次。

## 验收及接续
YAML五工作流解析、UI 11应用场景/清单一致和not_run报告、变更清单仍拒绝、现有契约/本地相关回归待执行；原失败证据来自远端run38669818315/38669606173和会话本地诊断，另存output/delivery-sync-20261015/ci060。受影响远端CI待实际结果，不以本地主机替代。附件下载复核同时继续，A133测试后置。
