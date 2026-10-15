# REQ-MIMI-PRIVATE-066 涂鸦一机一码运行适配与公共禁用
2026-10-15实施前记录，关联EDITION062/PRIVATE064/065、UPDATE001。

## 范围和流程
从产品tuya_main.c抽取授权读取/等待与设备初始化到产品私有adapters/tuya组件，保留公共TuyaOpen上游库、普通对话/DP业务和共享桌面。公共固定stub始终不支持授权；user_main在任何SDK/CLI/线程/网络初始化前检查禁用并返回。私有显式组件/来源锁/公共接口版本，缺失或篡改在prepare清理前拒绝，不从旧工作树/凭据自动启用。
流程：固定源接口与私有实现→私有审计及回读→公开stub/提前拒绝→prepare按明确profile复制唯一实现/回执→skip-prepare及直接build校验暂存源/回执→A133交叉编译和主机受控回归→结果与构建来源固定。失败保留历史，不写设备/密钥，不覆盖原rc2/rc23。

## 不变量与验收
原5000ms未授权重试和正常私有init参数/网络回调保持；公共即使已有凭据也不读取或初始化Tuya设备。原许可保留，不复制整个TuyaOpen或普通应用进私有仓库。临时构建源须验证明确profile、接口/源/工具SHA；工作树残留不能将公共构建变私有。native假API覆盖无设备的公共拒绝、私有重试/参数传递和初始化失败；实际A133编译必须另记，未跑不记passed。
系统init/打包目前仍强制启动Tuya，需要同时确认公共禁用的安装/启动选择，避免公共退出后反复拉起；完整产品锁/双产品镜像和公共应用更新保留私有组件仍需实测，不能只以主机stub通过称全部隔离。A133物理按用户延期，真实授权/云/设备控制not_run。证据output/private-tuya-adapter-20261015。

进一步核对发现SDK service_ai_runtime.c在拉起Tuya前独立读取factory授权，故隔离范围必须含后端三个reader函数。已建立公共ABI/固定禁用实现、私有重试初始化C和原字节reader函数头及来源锁；这只是组件阶段，尚未改当前SDK/主程序/packaging调用链，不能宣称整个公共产品已禁用。后续SDK公共/私有recipe与源码闭包必须一起更新并真实编译验证。

公共ABI/两固定禁用stub已加入integrations/tuyaopen/pairing，2个实际native编译/运行检查passed：公共运行stub无需链接任何设备授权/网络API，公共reader调用open即abort的探针未发生IO。仍尚未替换当前SDK/主入口，不能冒充整个产品已禁用。

组件阶段真实结果：4个对象使用原A133 GCC和真实Tuya SDK头编译，public/private runtime分别149输入、private/public reader分别71/4输入，公开组件无私有依赖，全部ELF64 AArch64且前后输入SHA稳定。公共2/私有2 native探针passed（reader14场景），详见a133-objects.json/verified-result.json。这里只编译适配对象，主程序/当前SDK/打包尚未接入，完整程序/镜像与物理not_run；不能把这个阶段改写为产品隔离通过。

清单接续校正：此前EDITION-SOURCE-MAP文案替换未命中旧整句，现按实际组件阶段同步，明确主链未切换，不改变代码或重跑已通过构建。私有753bc51远端CI已经success；公共c793fbe相关CI以remote-source.json实际状态为准，不提前记通过。

本轮接线先建立Provider edition-v2：原SDK工具d2/旧两profile/其他服务保持原字节，新增SDK recipe/共享AI源覆盖和公共reader stub，不复制整份后端或桌面。AI源只将已固定的reader函数片段替换为同目录头并填空行保留后续行号，私有头来自产品私有组件。先以原工具真实编译新源、native读取探针及公共二进制无授权路径检查建立新参考，再固定reference_backend_sha256/源图摘要；原original_backend_sha256保留为旧基线，不为消除差异而假称旧字节通过。新入口比较新源参考，记录与旧基线是否不同，拒绝错误版本/来源。通过标准SDK overlay归档/重放/回读后才新增到实际SDK。100ask锁按新recipe版本更新metadata而不改两C；Tuya锁单独固定，避免来源循环。

Provider edition-v2 actual SDK source replay passed: cd92e9bb, 4283 overlay files/95 baselines, 7 additive files, zero prior actual SDK files changed. Original d2 builder and historical profiles remain byte-identical. New source reference public0075ec88 differs from old84af as intended (no credential fields/path); private8a8b3a45 remains byte-identical. Explicit entry preflight public/private and 13 boundary tests passed; final product-entry full builds not_run until this code commit. Tuya main/prepare/build/install remain unconnected and not qualified. Existing mimi prompt encoding repaired without changing scheduler/rate or delivery priority.

Provider entry real builds5259757/private289feb8 now passed: public30TU SHA0075ec88 (intentional new source difference); private32TU SHA8a8b3a45 (same as prior), exact new pinned reference and no original-source dependency. Update regression185 passed with3 SDK-dependent classes skipped; attempted explicit SDK run had3 setup errors because invoker used wrong component directory names (recorded), corrected9 actual SDK audits allpassed; architecture75 passed with correct Provider input. Combined update194 unique checks, no outstanding skip. These are host/source checks, not devices.
Tuya main/prepare/build implementation now uses the common ABI before SDK/CLI initialization, explicit edition and fixed private component; prepare preflight runs before clean, stale receipt/source rejects direct/skip-prepare builds before venv/config. Six native staging boundary checks passed. New product build receipt binds actual ELF64 AArch64 binary SHA and selected inputs; full Tuya build not_run until this code is committed. Packaging/init/full product locks still pending.

打包接线实施前：产品构建/固件入口/SDK薄包均先核对public/private Provider与Tuya源码回执、实际二进制SHA及架构；错误profile/旧回执/混合产物在SDK或rootfs修改前拒绝。公共薄包不安装Tuya专有设备程序/模型/PID与工厂授权目录，私有显式启用且保持运行时授权。不再自动复制secrets/目录中的cloud配置进通用镜像，私有运行时仍可读取设备上原受保护配置，内建默认服务地址保留。init从固定edition文件选择已安装运行路径，未知值拒绝；不使用现有runtime_from_env不识别的none伪禁用。验收使用真实新程序+主机make安装夹具，不能称完整系统镜像或实机通过。

集成实际结果：公共Provider30TU0075ec88/private32TU8a8b3a45（私有旧字节一致）；完整Tuya程序public597e0e8a/privatefd96df76，ELF64 AArch64，显式来源/接口与最终程序SHA回执passed。完整更新204+新增固件写前拒绝1=205唯一检查、75架构0skip，私有2native（reader14场景）、源码审计passed。主机make安装真实新程序，公共不含专有设备程序/模型/PID/授权，私有保留程序且不带凭据；混用程序在任何rootfs复制前拒绝。新init固定edition路径/未知拒绝，系统构建先核对程序再碰SDK。说明PRODUCT-EDITIONS.md，证据integration-result.json。原rc2/rc23和设备未改，最终完整镜像/产品锁、共享应用更新后私有数据保留和物理均not_run/deferred。历史失败CRLF及调用方SDK路径写错已保存并修复。

CI收尾修复实施前：ee54公共21/私有755的5文件远端blob逐字节回读passed；私有CI/公共追溯passed。公共两个CI首次在setup-go固定b7ad提交的codeload压缩包404处失败，保留annotations并仅重跑一次，非源码测试失败。官方API和git refs确认提交，实际git fetch/checkout同一b7ad提交passed；因此三条既有Go工作流改为固定checkout官方同一提交到临时忽略目录，再执行local action。Go版本/输入、动作代码SHA、任务权限与验收不变；不改浮动tag、不下载另一份未固定代码。临时目录不进入源包，不重建产品。工作流YAML结构/固定版本核对及远端CI待真实重跑。
