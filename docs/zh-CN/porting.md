# 硬件移植手册

## 固定流程

1. 获得可合法使用的 SDK、开发板、原理图、启动/救砖流程和串口电平资料，私下记录版本与哈希。
2. 完成 `platforms/<id>/platform.json`；在真实验证前保持 `skeleton` 且 capability 为空。
3. 实现 toolchain/sysroot 映射，CMake 不写本机绝对路径，本地 SDK 由 manifest 环境变量定位。
4. 按 `core/ports/aitvbox_ports.h` 实现唯一 Provider。厂商头文件、内存布局、设备节点和 SoC 宏只能存在于该平台目录。
5. 增加内核、DTS、rootfs overlay、确定性打包和厂商对应的 boot-chain 安全门禁。
6. 每次只开放一个 capability；必须同时通过主机契约测试、交叉编译和真机测试。
7. 完成 validation manifest，执行干净构建、打包、烧录并归档证据后，才能把状态改为 `validated`。

## 能力验收

- Display：分辨率、格式、stride、旋转、背光和异常状态。
- Capture：信号检测、缓冲区/缓存一致性、JPEG/H264、热插拔和长稳。
- Input：描述符、报告格式、唯一拥有者、主机枚举、急停释放和维修回退。
- Audio：采播设备、重采样、默认 50% 音量、防扬声器自触发和真人打断。
- 网络/蓝牙：持久身份、重连、共存和不跨层回调的恢复逻辑。
- Sensor/LED：单位校准、设备缺失、LED 25 Hz 限制和相同帧抑制。
- Update：分区、非活动槽写入、签名、断电、健康提交和回滚。
- Identity：工厂数据只能来自受保护运行时存储，不进源码和日志。

A133 是参考平台；A527 不能假设复用 A133 多媒体 API；V883 必须先确认厂商媒体与打包模型；RK3576 的 MPP/RGA、DTS 和 loader 只放其 Provider；RV1106 当前按 32 位 ARM 管理，必须重新审计内存和 ABI。

## 视频 Provider V2 移植

1. Capture Provider 必须是物理视频节点的唯一拥有者。显示、编码、快照只订阅帧，禁止再次打开设备节点。
2. 优先输出 DMA-BUF；无法导出时可使用 CPU plane。每个 plane 必须填写真实 `stride`、`offset` 和 `size`，不能假设 `width == stride`。
3. `acquire_frame` 成功后，帧及其 fd/指针必须保持有效，直到调用方执行 `release_frame`。超时返回 `AITVBOX_TIMEOUT`，断信号不得伪造黑帧为正常输入。
4. Display 和 Encoder Provider 不得长期持有帧；异步硬件需要在完成回调后再释放。切换分辨率时先停止分发，排空旧帧，再重建 buffer pool。
5. 先实现状态、热插拔与快照，再接本地显示，最后接硬件编码。每一步都验证“只有一个进程持有视频节点”、帧计数持续增长和拔插自动恢复。
6. A527/V883 应将厂商 VIN、2D 修复和 VENC 封装在各自 Provider；RK3576 使用 V4L2/RGA/MPP；RV1106 使用对应 VI/RGA/VENC，并重新核验 32 位 fd、指针和物理地址宽度。上层 Capture Daemon 和 Socket 协议保持不变。

参考实现、控制协议、编译和板端验收见 [Capture Daemon](capture-daemon.md)。
