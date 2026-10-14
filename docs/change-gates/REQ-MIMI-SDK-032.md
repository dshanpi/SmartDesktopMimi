# REQ-MIMI-SDK-032 从已恢复输入做干净产品重建

2026-10-14实施前门禁，基准3e1dc3bcb4699d0a7e9fdd0591e6bc39168818fc。承接SDK恢复/补充闭合/bitmap修复/clean metadata和UPDATE001/TRACE001。现有97组件已经独立恢复，r11完整镜像已生成；尚缺空编译目录的整产品重建，不再把增量构建说成clean。

目标是复现既有system1.0.0-rc.1/factory1.0.8-rc.13产品基线，构建来源固定640c026050e9b8184c512b9dfb3ccf8d2ce8e0c1，私有SDK输入及overlay沿用已记录快照。为避免改写当前开发源码，在开发目录.delivery/system-clean-source-20261014建立固定提交的独立工作树；这是必要构建来源，不在开发路径外创建源码。旧标签不移动，不产生发布或上传。

流程：确认SDK无活动构建/43GiB余量→验证现有factory baseline与启动黄金输入→保存97组件当前commit/diff摘要、根配置和保护文件→复制已验签工厂基线至固定工作树并check-only→把SDK out/和kernel/linux-4.9/output编译缓存原子移入同.delivery专用恢复目录（不删除、不重复整SDK）；从已固定的根配置/黄金启动镜像重建最小pack输入，生成新的产品包索引并要求五包完整→从空kernel/OpenWrt编译缓存执行完整产品编译与pack→实际rootfs读回/签名/启动链/bitmap门禁→记录hash和源/SDK输入。出现空间不足或输入变化立即停止，不关闭门禁；失败目录和旧缓存都保留，按核对后恢复。

不变量：Boot0/U-Boot/BL31等受保护输入只读，不重编启动链；原SDK和所有已生成r11产物保持。用当前已通过的包索引检查器补充历史构建脚本的预检，记录独立checker来源与hash，不修改历史源码或伪造干净提交。旧factory13专供这个系统复现，不能覆盖板上rc17或冒充最新应用发版。工厂数据/设备凭据/单机身份和私钥不复制入公开包。T527/RK3576仍未验收。A133031省电观察不被重建触碰。

本批开发文件：本门禁、索引及不可变批次。实际构建脚本/输入摘要/原始日志存output/sdk-clean-product-20261014，恢复缓存和构建产物都在.delivery。当前not_run：工作树check-only、缓存移动和完整clean构建；不提前标完成。后续真实镜像升级回退/断电/数据继续独立门禁，禁止发布。

准备实际结果：首次check-only缺固定epoch被拒，r2绑定1791932282后passed。97组件HEAD/现有差异和八个启动输入已绑定；out/kernel output原子移至.delivery/sdk-cache-before-clean-20261014，未删除。新pack输入、黄金镜像及新生成五包索引passed。首次build supervisor在产品启动前拒绝staging_dir存在。复核为本轮metadata预检生成的4个regular host bootstrap文件及25个/usr/bin工具链接，目标均root拥有不可普通用户写，固定resolved/哈希；不含原build_dir/staging包缓存。两次汇总断言区分regular/link的日志说明保留；后续仅完全匹配fresh-bootstrap-files.json才启动，不泛化接受未知缓存。kernel output仍空、build_dir仍无。

完整干净产品重建已实际启动：supervisor3623310/child3623311，output/sdk-clean-product-20261014/build-result.json phase=running；正在从新目录编译工具/产品依赖，最终kernel/rootfs/镜像门禁仍not_run。旧缓存保留恢复用，8GiB动态磁盘保留线触发会停止并记录失败；源码固定640c026与旧factory13，不是板端新安装或最新应用发版。下一轮读取现有job结果，禁止再启动另一个SDK构建。


构建等待期间补充只读输入核对（实施前）：按私有repo overlay manifest逐项核对4114个真实文件/链接在当前SDK中的内容摘要，补充97组件tracked diff检查未覆盖的未跟踪输入。原SDK组件快照/57项supplement/独立build-fix仍是恢复权威；overlay差异只列出并逐项关联原材料和产品注入，不自动认作丢失或修改构建中的文件。输出output/sdk-clean-product-20261014/overlay-input-readback.json，既有文件不覆盖，秘密/文件正文不打印。所有差异未解释前full input closure保持not_run，不以核对数量替代干净构建及真机验收。


扩展输入只读核对：4109个regular文件哈希一致；四个启动链接的manifest字段为link，首次比较器使用target/link_target误报review，原记录保留，四个真实readlink与保存文本一致passed。共4113/4114条匹配；唯一defconfig在当前SDK构建中被临时规范化，最终恢复及97组件差异检查待job结束，仍not_run。无SDK修改/重复构建。证据overlay-input-readback.json和overlay-input-review.json；不把临时配置差异泛化为允许其他源输入变化。


clean范围纠正：发现Tina实际KERNEL_BUILD_OUT_DIR是源码根，output只是staging；旧sunxi_wdt.o/vmlinux/generated头早于本job开始，vendor build_kernel不执行clean。032内核空缓存条件failed，不能将supervisor可能返回passed等同完整clean；OpenWrt缓存重建真实结果仍保留。补做036单组件保存输入重放，旧job继续不被打断。


2026-10-14本轮实际结果：纠正clean范围：Tina内核在源码树编译，output仅staging，旧.o/vmlinux/generated早于032启动。故完整内核空缓存条件failed；032继续实际OpenWrt构建不重启，不将未来脚本passed当全SDKclean。036独立重放已完成，原SDK未修改，待032终止再核对激活和重编受影响镜像门禁。


2026-10-14实际结果：首轮terminal failed1：IPKVM64编译器未初始化；97源码tracked差异[]、8保护差异[]，原配置已恢复；完整SDK镜像未完成。038真空缓存kernel全编译补齐后，产品r2复用本轮新OpenWrt缓存继续原source640c026/factory13基线，不用旧保留cache。r2首supervisor缺新目录metadata receipt在构建前失败，原异常保留；实际当前metadata再检通过后重启尚未开始的wrapper，supervisor344068/child见真实receipt，产品验收仍not_run。
