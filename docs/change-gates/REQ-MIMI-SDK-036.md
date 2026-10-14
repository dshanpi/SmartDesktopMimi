# REQ-MIMI-SDK-036 真正的内核空缓存重放

2026-10-14实施前；纠正SDK032范围。Tina mkcmd.sh将KERNEL_BUILD_OUT_DIR设为kernel/linux-4.9，output仅KERNEL_STAGING_DIR。当前源码树sunxi_wdt.o/vmlinux/generated头明确早于032开始，build_kernel只清output并增量make；因此032可用于OpenWrt空缓存重建，不能宣称完整内核clean。

流程：保存旧缓存事实与实际脚本绑定→只在独立.delivery目录从固定kernel组件Git commit d7181bd0d3d60205fe20128c4ab267d2fb57884a及保存snapshot tracked.patch/286未跟踪输入、supplement内核.config重放新源码（不复制旧.o/.cmd/vmlinux/generated缓存）→核对snapshot/supplement/归属manifest、HEAD/diff/全部输入/config哈希及独立Git对象，无alternates/硬链接依赖→现有SDK032真实job终止前禁止替换canonical kernel→终止后再次核对原源输入与配置没有变化，将旧kernel整目录原子移动至已核对.delivery恢复路径、把重放源移回SDK固定kernel路径→内核全编译、产品镜像及签名/启动链/rootfs门禁重跑。OpenWrt032本轮新缓存与固定应用产物按真实验证范围复用，不重复恢复整SDK；若现有发布脚本不支持可靠的阶段重跑，则仍使用完整门禁路径，明确因内核clean未达标而重跑，不伪造缓存为空。

安全不变量：不在活跃SDK构建中清理/替换内核，不改任何Boot0/U-Boot等八个保护输入；不覆盖旧失败或032记录。路径必须在同一.delivery根内、无符号链接跳转，旧目录保留恢复，任何校验失败不激活。tar仅允许已登记且哈希/类型/模式符合的文件，拒绝路径越界、Git元数据覆盖和外逃链接；从旧仓库只读取得固定Git对象，不拿旧编译缓存或原未保存源码充当恢复来源。补充.config按保存摘要固定，不猜默认配置。独立源准备不等于编译/镜像/真机通过。

实现计划tools/update/replay_kernel_component.py及针对实际Git/tar/激活守卫的测试；证据output/kernel-clean-replay-20261014，独立准备.delivery/kernel-clean-component-20261014，激活旧树恢复路径.delivery/sdk-cache-before-clean-20261014/kernel-source-cache。适用UPDATE001/TRACE001、用户SDK overlay和关键备份要求。当前not_run：工具、保存输入独立重放、终止后激活/全编译/产品门禁；设备/发布仍未验收。下一步不重启SDK032或被动UART035，等待各自真实结果。


工具实施：prepare独立clone固定组件并从已保存patch/tar/config恢复，原SDK只读；activate要求旧构建真实terminal、原PID和childPID消失，且无其他SDK编译进程，重新逐项核对原源输入/模式/config及新树无非源缓存，再原子移动保留旧树；第二步rename失败恢复原位。13项真实Git/tar/临时目录测试passed（源码/config重放、旧cache排除、错pin/源漂移/未捕获新源/容器内坏内容/越界/.git覆盖/外逃链接/活跃job/PID/符号链接/带cache激活拒绝、实际终止后保留旧树激活）。首次测试两条失败是预期拒绝原因断言及fixture缺vmlinux忽略规则，守卫均拒绝，原log保留；修正fixture/断言后r2通过。实际SDK单组件重放/激活/全编译仍not_run，不复用fixture作为SDK验收。
