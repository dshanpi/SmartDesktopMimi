# REQ-MIMI-SPLIT-061 A133共享采集/编码源归属收尾
2026-10-15 实施前记录，关联SPLIT053—057、DELIVERY058、MIMI-UPDATE-001和MIMI-TRACE-001。

## 背景、范围与流程
上层apps/ipkvm/video仍直接保存A133 V4L2/ION/硬件H264采集实现及编码补丁。仅将该目录5个实际追踪输入及原许可证移至私有SDK标准overlay独立smartdesktopmimi-a133-capture组件；Web前端/业务服务保持原处，不因进程名把混合代码一并判定为硬件。
先以当前原源和固定SDK新目录构建参考，再保存原字节、工具链/实际源/头/链接输入和原编码补丁结果；SDK自含新目录编译与原程序同字节后，标准overlay重放、敏感边界检查、私有SDK授权上传及独立读回。通过后改系统构建入口/退休原源及SDK显式测试，再验证实际入口与受影响主机项目。失败保留、不删除未保存旧源、不回退原目录、不上传不合格补丁。

## 不变量、路径与验收
不改采集/显示/编码/缓存/超时/协议与ABI，HDMI默认关闭且本轮不操作设备；原程序与原C++/头/补丁字节保留。编译器固定原实际Linaro5.3，厂商源仍只从SDK取，不复制到公开源码包；来源/闭包越界或缺失在清理/构建前拒绝。标准overlay入口不变、只新组件装入实际SDK，不全量覆盖已审核系统输入；可信钥/绑定/持久数据不变。rc2/rc23及已上传25附件/标签冻结，源码迁移不冒充旧镜像来源。原失败不删改，未提供SDK的实现测试明确not_run。
计划位置scripts/build_a133_capture.py、scripts/build_apps.sh、apps/ipkvm/Makefile及video源退休、tests/sdk_source.py和平台fixture/归属策略、SDK overlay/meta。起点应用caae6de4d29ffb538aa6640799bb043816c0d2ae、SDK7ed3fdd779cfdd171ddf54063e63a5c074314c95；实际提交/锁/ABI/目标证据output/capture-source-split-20261015。
主机/源码归档及独立编译/回读/入口验证目前not_run；真机/新镜像deferred_not_run，用户待提供新SDK Ubuntu环境。完整迁移migration_complete=false，Tuya/混合业务/冻结客户端/最终SDK及产品来源锁仍未完成。

## SDK源保存阶段完成，后续入口仍未切换
私有SDKa49d1d11cc65e5832f77cb3cfcbd215054aa4641已推送并独立fetch/官方verify_overlay读回4276文件/95实际组件passed，旧4267项不变。9新增文本输入/原许可证及已知凭据签名筛查0finding；组件不含厂商4TU，仍固定SDK实际源/哈希。原fresh/staged/helper/重放后编译全部74ceb07同字节（81304字节）、142实际编译输入和28实际链接输入/脚本及别名固定。锁9f7d4f5e9535836bc570eb557c5ef26e840f4d6de033a6462101e564ba4aaa71。
初诊旧binutils的-t库输出形式未识别，只列5项，诊断不完整；未将该锁上传。修正解析旧-lfoo(path)/archive形式，并加--verbose真实成功打开记录，完整28项后重新编译原字节passed；link-trace-correction.json保留纠正。
用户同时提出EDITION062产品边界，当前先梳理该新要求；本批只完成SDK源归档，不删除公共apps/ipkvm/video，不安装新组件到开发SDK、不切换构建入口。余下上层入口/SDK显式测试/源码退休仍not_run，物理deferred_not_run，原rc2/rc23和25远端附件不动。

## 上层入口与源退休实施
本批接续SDK归档：只9新组件文件安装实际SDK，scripts/build_a133_capture.py固定原锁，产品build_apps在清理/依赖前预检；IPKVM all先检查固定组件，video新目录独立编译/归集收据，web目标仍独立。原5追踪实现/Makefile/补丁在SDK重编/远端回读后删除，退休路径/作用域守卫保留。实际make video入口passed，程序74ceb07同字节，不重编无关网页/桌面/Tuya或整镜像。
测试显式MIMI_A133_CAPTURE_ROOT；缺SDK保持not_run。旧fixture读SDK头，保留监督测试并新增既有缓存syscall seam的自动执行，open/ioctl/close包装不接触设备。入口篡改/旧输出保留、GNU旧/新ld路径和脚本解析、make元字符、外来依赖逃逸等检查待运行；真机后置。一次接续文档脚本误以为apps/ipkvm/README.md已存在而中止，未影响构建/设备，后续新建该归属说明；已写入的测试不重复追加。

## 上层入口与受影响验证完成（代码45ba139）
实际SDK9新组件独立编译、make video新入口/归集、原74ceb07字节/ELF64little/AArch64和解释器/NEEDED、缺固定组件的make all提前拒绝且旧输出保持、桌面672源按新增退休策略复核、公有源码审计均passed。34边界含2 native smoke、74架构、74平台全部0skip；native2重叠后唯一180项，不重复报182。缓存/监督测试仅syscall seam/线程状态，不打开摄像头/ION。源/包装器/库路径篡改和不支持根字符均拒绝。SDK原配置、618镜像/cdf SWU/c22应用包SHA全部保持。verified-result.json绑定实际代码/原源/SDK/锁/ABI/测试与保护哈希。
原5上层源已退休，新构建入口完成；公开/私有专有配对仍062未实现，完整源迁移false，真机和完整新镜像未执行。修正当前编译文档的已退休路径，不更改历史快照/标签/产物或归属旧通过证据。
