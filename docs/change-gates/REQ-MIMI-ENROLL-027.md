# REQ-MIMI-ENROLL-027 Ubuntu主机受控登记

2026-10-14 实施前记录，承接SDK025/SESSION026。适用UPDATE001/TRACE001；基准6fec60cc37dd53c47ba9de14fd49c24ac785d20d。

## 背景与边界
当前实际目标是Ubuntu20.04/x86_64开发主机（VMware虚拟机），不能把它登记为A133或宣称物理单板验收。读取固定DMI节点生成板型/修订/产品指纹和虚拟机实例证据；UUID只以哈希记录。DMI版本原值为None时仍以实际字节指纹匹配，不编造PCB修订。首轮只支持Ubuntu20.04 x86_64；其他OS/架构不猜测。Provider0.2.0/Ports2为已实现平台契约，仍需实际会话握手，登记不替代运行验收。

## 流程及回归不变量
只读检查OS/架构/libc/固定DMI→生成device/enrollment候选→使用root控制目录保存后才能执行公共登记检查；检查时重读固定系统节点并逐项配对，不以两个JSON相同当作登记。缺节点/无效UUID/目标错误/证据路径或哈希异常立即拒绝。A133保留board+silicon证据流程，Ubuntu单独使用board/revision/product/machine，不将VM UUID称为物理芯片ID。不得放宽现有trustedFile/root路径门禁，不读取或替换密钥，不自动安装/启动/上传，不修改主机网络或其他服务。

## 实现及验收
计划services/mimi-update/update/ubuntu_identity.go与CLI只读inspect-ubuntu入口；登记检查按platform分派；严格路径/目标/原始证据匹配及A133回归测试。只读生成候选包含实际设备和enrollment，但不自动部署。需要验证实际主机读取、受控root登记检查、篡改目标/证据拒绝；实际日志绑定源码和updater哈希。

- not_run：代码/主机回归、实际主机采集和受控检查。
- not_run：签名应用/deb打包激活、更新回退与数据矩阵。
- blocked：A133失联根因和完整系统验收仍未解决。

证据output/ubuntu-enrollment-20261014/，下一步以实际登记匹配签名应用，继续平台生命周期与更新器集成；不把登记成功记为平台全面交付。

实施细化：当前仅接受实际VMware Virtual Platform / Ubuntu20.04 / x86_64 / glibc2.31组合。首轮范围固定为本开发主机，不泛化全部Ubuntu硬件。Provider0.2.0与Ports2是已实现的受控平台契约；登记本身未证明活跃Provider，SESSION026及后续平台启动健康必须独立验证。新增inspect-ubuntu --baseline只输出候选JSON，根目录部署由单独受控操作完成；输出不含原始UUID。公共identity入口针对ubuntu-host重读固定节点，A133分支保持原语义。

提交前Go整组回归passed（新增3组含目标、身份字段、运行库、路径、哈希、重复证据等子用例）；实际采集/可信root目录校验与跨架构编译待固定提交后执行。受控登记目录将使用/var/lib/smartdesktopmimi-platform/ubuntu-enrollment-20261014（只保存本平台运行记录，不含源码/私钥），因为现有trustedFile要求全部祖先root所有且不可被普通用户写，开发目录不能冒充可信平台配置；候选源码/构建/证据继续在开发目录.delivery和output。不覆盖任何现有同名目录。

## 2026-10-14 实际登记及拒绝验证
实现源码6b024f3fd9b69173c61a676d02801cebc4a26dfe。原生updater SHA256 a782e305820cdb155b43152b0a51784af9a01f3f0ce9e031328865f62f6030d1；A133交叉updater SHA256 5423ec84bfc34566c26c9bfe3d21789af061d4763bb24f2e9faaea407cec534d，两者编译passed，未给A133安装新updater。

实际主机inspect-ubuntu只读采集passed；受控root目录/var/lib/smartdesktopmimi-platform/ubuntu-enrollment-20261014已保存device/enrollment，不含私钥/原始UUID。实际check-enrollment重读真实DMI/OS/libc通过；4类实际拒绝passed：普通用户可写来源、错误架构、错误证据路径、错误机器哈希。负例单独文件已清理，实际登记保留。平台updater配置/服务未安装。证据output/ubuntu-enrollment-20261014/result.json和对应日志；这不是签名系统基线或签名应用激活。

Go全组、116项更新测试、74项架构回归passed。注册仍只覆盖受控硬件/系统基础档案；实际应用链接了SDL/ALSA/curl等库，完整SONAME版本及运行库内容摘要尚未纳入本Ubuntu设备档案。现有签名打包门禁要求这些依赖，不能靠补造版本或放宽检查签包。下一步采集并持续核对实际运行依赖，再完成平台会话/更新器启动与健康、签名amd64包和实际安装升级回退。SESSION026 rc19不因本平台工具修改冒充新应用产物。无上传，整体交付未完成。
