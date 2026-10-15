# REQ-MIMI-SPLIT-061 A133共享采集/编码源归属收尾
2026-10-15 实施前记录，关联SPLIT053—057、DELIVERY058、MIMI-UPDATE-001和MIMI-TRACE-001。

## 背景、范围与流程
上层apps/ipkvm/video仍直接保存A133 V4L2/ION/硬件H264采集实现及编码补丁。仅将该目录5个实际追踪输入及原许可证移至私有SDK标准overlay独立smartdesktopmimi-a133-capture组件；Web前端/业务服务保持原处，不因进程名把混合代码一并判定为硬件。
先以当前原源和固定SDK新目录构建参考，再保存原字节、工具链/实际源/头/链接输入和原编码补丁结果；SDK自含新目录编译与原程序同字节后，标准overlay重放、敏感边界检查、私有SDK授权上传及独立读回。通过后改系统构建入口/退休原源及SDK显式测试，再验证实际入口与受影响主机项目。失败保留、不删除未保存旧源、不回退原目录、不上传不合格补丁。

## 不变量、路径与验收
不改采集/显示/编码/缓存/超时/协议与ABI，HDMI默认关闭且本轮不操作设备；原程序与原C++/头/补丁字节保留。编译器固定原实际Linaro5.3，厂商源仍只从SDK取，不复制到公开源码包；来源/闭包越界或缺失在清理/构建前拒绝。标准overlay入口不变、只新组件装入实际SDK，不全量覆盖已审核系统输入；可信钥/绑定/持久数据不变。rc2/rc23及已上传25附件/标签冻结，源码迁移不冒充旧镜像来源。原失败不删改，未提供SDK的实现测试明确not_run。
计划位置scripts/build_a133_capture.py、scripts/build_apps.sh、apps/ipkvm/Makefile及video源退休、tests/sdk_source.py和平台fixture/归属策略、SDK overlay/meta。起点应用caae6de4d29ffb538aa6640799bb043816c0d2ae、SDK7ed3fdd779cfdd171ddf54063e63a5c074314c95；实际提交/锁/ABI/目标证据output/capture-source-split-20261015。
主机/源码归档及独立编译/回读/入口验证目前not_run；真机/新镜像deferred_not_run，用户待提供新SDK Ubuntu环境。完整迁移migration_complete=false，Tuya/混合业务/冻结客户端/最终SDK及产品来源锁仍未完成。
