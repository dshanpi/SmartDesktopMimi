# REQ-MIMI-SDK-036 真正的内核空缓存重放

2026-10-14实施前；纠正SDK032范围。Tina mkcmd.sh将KERNEL_BUILD_OUT_DIR设为kernel/linux-4.9，output仅KERNEL_STAGING_DIR。当前源码树sunxi_wdt.o/vmlinux/generated头明确早于032开始，build_kernel只清output并增量make；因此032可用于OpenWrt空缓存重建，不能宣称完整内核clean。

流程：保存旧缓存事实与实际脚本绑定→只在独立.delivery目录从固定kernel组件Git commit d7181bd0d3d60205fe20128c4ab267d2fb57884a及保存snapshot tracked.patch/286未跟踪输入、supplement内核.config重放新源码（不复制旧.o/.cmd/vmlinux/generated缓存）→核对snapshot/supplement/归属manifest、HEAD/diff/全部输入/config哈希及独立Git对象，无alternates/硬链接依赖→现有SDK032真实job终止前禁止替换canonical kernel→终止后再次核对原源输入与配置没有变化，将旧kernel整目录原子移动至已核对.delivery恢复路径、把重放源移回SDK固定kernel路径→内核全编译、产品镜像及签名/启动链/rootfs门禁重跑。OpenWrt032本轮新缓存与固定应用产物按真实验证范围复用，不重复恢复整SDK；若现有发布脚本不支持可靠的阶段重跑，则仍使用完整门禁路径，明确因内核clean未达标而重跑，不伪造缓存为空。

安全不变量：不在活跃SDK构建中清理/替换内核，不改任何Boot0/U-Boot等八个保护输入；不覆盖旧失败或032记录。路径必须在同一.delivery根内、无符号链接跳转，旧目录保留恢复，任何校验失败不激活。tar仅允许已登记且哈希/类型/模式符合的文件，拒绝路径越界、Git元数据覆盖和外逃链接；从旧仓库只读取得固定Git对象，不拿旧编译缓存或原未保存源码充当恢复来源。补充.config按保存摘要固定，不猜默认配置。独立源准备不等于编译/镜像/真机通过。

实现计划tools/update/replay_kernel_component.py及针对实际Git/tar/激活守卫的测试；证据output/kernel-clean-replay-20261014，独立准备.delivery/kernel-clean-component-20261014，激活旧树恢复路径.delivery/sdk-cache-before-clean-20261014/kernel-source-cache。适用UPDATE001/TRACE001、用户SDK overlay和关键备份要求。当前not_run：工具、保存输入独立重放、终止后激活/全编译/产品门禁；设备/发布仍未验收。下一步不重启SDK032或被动UART035，等待各自真实结果。


工具实施：prepare独立clone固定组件并从已保存patch/tar/config恢复，原SDK只读；activate要求旧构建真实terminal、原PID和childPID消失，且无其他SDK编译进程，重新逐项核对原源输入/模式/config及新树无非源缓存，再原子移动保留旧树；第二步rename失败恢复原位。13项真实Git/tar/临时目录测试passed（源码/config重放、旧cache排除、错pin/源漂移/未捕获新源/容器内坏内容/越界/.git覆盖/外逃链接/活跃job/PID/符号链接/带cache激活拒绝、实际终止后保留旧树激活）。首次测试两条失败是预期拒绝原因断言及fixture缺vmlinux忽略规则，守卫均拒绝，原log保留；修正fixture/断言后r2通过。实际SDK单组件重放/激活/全编译仍not_run，不复用fixture作为SDK验收。


实际首次prepare在clone前拒绝：SDK比旧286项snapshot多一个drivers/media/platform/sunxi-vin.tar.gz；该包已在私有5e947 repo overlay真实保存，是用户要求保留的下载/源码包，不是cache，不能忽略。stage未创建、活跃SDK未修改。扩展实施前：prepare显式读取固定overlay commit和该commit内manifest，校验当前manifest及每个kernel覆盖条目的实际字节/模式；与snapshot重叠必须一致，新增条目合并入恢复清单，禁止覆盖冲突。只在已有snapshot基础补充已保存repo overlay，不从活跃树拷贝未知材料。下一步按该完整输入准备并补充overlay来源/冲突/缺失拒绝测试。


repo overlay补充实现及17项测试passed：prepare CLI显式要求overlay路径与固定commit；manifest必须与该commit Git blob一致、LFS/文件实际摘要可用，kernel重叠来源内容冲突拒绝，合法新增下载包合入恢复清单。原树tracked文件非执行权限因先前恢复umask与overlay不同，只读验证保留字节/执行位；准备新树按overlay完整mode恢复且再次检查，原树不改。新增测试真实保留snapshot之外archive、错误overlay commit、缺少LFS body、来源冲突拒绝，原13项回归仍通过。首个真实prepare的pre-clone拒绝保留，r2将使用完整保存材料；没有忽略该下载包或复制未登记活跃源码。


真实r2在clone前拒绝schema差异：旧snapshot 149个重叠条目只有mode/hash，repo overlay另有size，字典全等错误将新增元数据认成内容冲突。源哈希与mode实际一致，旧失败保留。按实际保存schema修正：重叠严格比较类型/内容hash/link/mode，两侧都有size才比较size，overlay size用于丰富恢复条目；旧tar无size字段时仍验哈希并限制单文件512MiB，不放弃完整性检查。fixture改为真实旧schema，17项r4passed且验证合法archive/重叠、实际内容冲突依旧拒绝。r3实际准备待运行，原SDK/旧job不动。


真实r3拒绝已定位：仅GPU生成配置config_kernel.mk两个绝对路径从原SDK根迁到当前.delivery/sdk-restored，其余字节不变；文件已在固定snapshot归档，不能删除或重写保存摘要。实施前限定：仅该已登记文件，从验过容器/成员摘要的原始文本推导KERNELDIR、KERNEL_CROSS_COMPILE两行的当前SDK路径；原源只读校验允许原摘要或该精确推导摘要，其他行/路径/字段变化仍拒绝。独立新树继续恢复原保存字节，不从活跃源拷贝。记录原/迁移后摘要及路径来源，重跑合法迁移、额外字节改变及已有拒绝用例。当前实际SDK准备仍failed，激活/编译未运行。

限定GPU路径迁移实现后19项测试passed（r5），已验证新树恢复原保存字节、当前树仅两字段迁移可读回，改动KERNEL_ID仍拒绝。未改活跃SDK源或可信快照；实际r4准备待执行，原r3错误与具体差异证据保留。

真实r4在clone前发现同一GPU构建目录保存的绝对源码链接已随SDK搬迁，原链接/当前链接仅SDK根不同。实施前：逐项保存原链接来源，限定GPU kbuild内部且原目标属于保存kernel源码树；新树恢复成指向同一已追溯源文件的相对内部链接，禁止外部目标、路径穿越和未知目标。当前源只接受原/精确迁移绝对链接或等价内部相对链接，不接收任意重定向；原tar仍逐项核对原linkname，恢复映射与hash/config/HEAD一起验。原r4失败保留，未清理活跃SDK。

GPU链接迁移实现：132个已保存链接均仅SDK根变化，独立树变成绑定同一固定GPU源码的内部相对链接；记录原linkname和canonical精确迁移目标。21项r6真实Git/tar测试passed，新增内部绝对链接恢复及外部重定向拒绝。真实r5准备待执行；活跃SDK仍未修改。


2026-10-14本轮实际结果：真实r5准备passed：固定Git组件+saved patch/tar/286原输入+repo overlay额外archive+supplement.config独立源恢复，132个固定GPU源链接内部相对化；HEAD/diff/输入/config及无cache/Git独立对象检查通过，receipt生成。21主机回归passed。原prepare四次失败保留；活跃SDK未修改、激活与真实内核编译not_run。证据output/kernel-clean-replay-20261014/prepare-r5-result.json及新树.mimi-kernel-replay.json。

门禁覆盖首次检查failed：032的failed证据条目缺schema要求reason字段（实际失败状态/证据未变）。补齐既有条目的失败原因文本，原提交e546256和关键bundle保留原始记录，不改实际通过/失败事实；下一次覆盖检查重跑。关键新增源码bundle26388B与证据archive106885B已验证，仅增量材料、不含串口私有原文或SDK cache。
