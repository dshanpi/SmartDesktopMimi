# REQ-MIMI-UBUNTUAPPS-076 Ubuntu扩展应用与数据消费者

2026-10-19实施前记录。承接UBUNTU022、SDK025、DEBACTIVATE030、LOGIN052、MIMI-UPDATE-001，基准e569ba05d0aac2a5af1da6c271c043932a5ae89a。用户已恢复原Ubuntu开发机软件工作；A133、新SDK物理验收仍延期。

## 完整目标与边界
Ubuntu共享桌面应消费平台安装的扩展应用，签名安装/发行者归属、明确的架构/运行时准入、受限制执行、真实storage.app写入/重启读取以及桌面升级/回退后的应用和数据保持必须逐项完成。不能以复制文件、假服务回复或只校验哈希代替消费者验收。旧schema2和安装策略当前仅认AArch64，不能改字段冒充Ubuntu支持，也不直接复制A133 SDK私有服务源码进入公共应用。平台服务独立于桌面更新包；原信任、绑定、rc29、其他应用及用户数据不变。媒体能力仍是UBUNTU022独立未完项。

## 本批：解开共享桌面的固定A133路径
现状：user_app.c固定/overlay/aitvbox/apps和/var/run/aitvbox/apps.sock，Ubuntu并无这两个入口。使用已有平台配置LV_PORT_LINUX_CONFIG新增user_apps.root和user_apps.socket_path；默认维持原A133入口。显式空值禁用相应入口；相对/父目录/控制字符/过长配置拒绝为禁用，不截断、不回退到旧平台。配置属于开发/平台入口，不加入用户桌面设置。目录及socket分别配置；仅有目录不宣称执行服务存在。

流程：平台配置→默认/显式根路径→目录扫描→嵌入UI读入；invoke仍送平台appd验证权限。路径无效或目录/服务不存在→空目录/原失败提示，不启动或安装服务。测试真实C配置解析和真实LVGL读取独立临时目录的声明式页面、选择/关闭/重开；模拟页面不是签名安装验收。

## 验收与接续
实施前not_run：配置正反例、真实LVGL消费、共享完整编译。后续仍not_run：Ubuntu平台安装工具/目标准入、真实sandbox/storage.app服务、签名安装和运行、实际桌面升级/回退数据、真实logout/relogin。必须继续完成这些目标，不能以本阶段完成替代全项。证据output/ubuntu-apps-076-20261019；不得操作生产应用目录，不强制注销用户。

### 归属门禁纠正（实现过程中）
首次将字段加入system/config/app_config.*被source_ownership拒绝（shared-frozen），原失败和差异保留。未放宽锁、未重标SDK。改为src/apps/user/user_app_config.*应用侧独立客户端读取同一平台配置文件，冻结共享三个文件恢复基准原字节；不修改平台IPC配置/ABI/Provider。首次配置边界测试还复现NUL/超长行被截断接受，新增完整物理行读取后拒绝整份扩展入口配置；显式配置文件缺失也禁用两入口。需要按最终层次重新执行受影响检查。

## 本批实际结果
应用侧user_app_config.c/h与实际user_app.c接通；10项真实C配置检查passed。真实LVGL临时声明式目录读取、选择/重开与服务不可用失败检查在三尺寸/双语言/双主题共12图通过，另禁用service端点1组通过；没有使用真实appd，不能叫签名安装或storage验收。小屏布局已人工看图。源归属原锁73491272和IPC1.2通过，冻结文件字节保持。原归属拒绝与配置截断失败历史保留；完整新desktop构建、平台安装/架构准入、sandbox/storage、升级数据、真实登录与物理尚未执行。详见docs/validation/ubuntuapps076-client-20261019.json。


### 完整构建追加结果
干净源码866f7e0fbcc87a45fbec31d263a987be42cbf704使用原Ubuntu SDK锁完成desktop/Web构建，89.24秒退出0；28个暂存文件逐项SHA256通过，编译数据库确认新增客户端读取器实际编入。桌面SHA256 e988a25c20e1e819c270097b4a7c009757e5434e8c3380c58f887d8a63bf33fb。此为未签名、未安装测试候选，未替代rc29；前一阶段not_run记录保留为历史。未来签名必须换新版本重新形成干净源码构建证据。Ubuntu真实扩展安装/运行/storage与媒体完整数据、真实重登录、A133物理验收仍未完成。详见docs/validation/ubuntuapps076-build-20261019.json。


### 远端门禁失败与修复前记录
86c0d2c的Application update gates在UI coverage inventory失败（run39204787948），本地同命令复现。原因是user_app.c改动后遗漏更新已审阅页面指纹；三个回调绑定无变化，目录/端点行为已有最终13组LVGL证据。将补充平台目录、无效配置、禁用服务场景并重新记录inventory；此为覆盖清单修复，不修改应用二进制/平台锁，也不重复完整构建。原失败保留。


## Ubuntu平台安装事务：实施前（2026-10-19）
基准f904620。现有Tina安装器依赖jsonfilter及SDK policy，不能在Ubuntu假称已可用。本批新增独立Ubuntu平台安装器，复用公共aitapp校验器，保留签名/文件清单/发布者归属/版本拒绝与独立data。仅允许现有schema1无平台权限包；旧schema2 AArch64在Ubuntu明确拒绝，不能把同一ELF改机器编号冒充原生支持。后续必须完成显式原生架构/运行时准入、受限执行和storage broker，不以声明式安装替代完整目标。
流程：受控root配置和可信公钥→全局事务锁→恢复未提交事务→隔离解包/全部校验→发布者/版本/数据配额→持久journal→旧目录移走→新目录切入→提交journal移除；提交前崩溃恢复旧完整目录，初装恢复无安装。数据搬运期间必须与后续broker共用锁。持久日志只记录应用ID及随机内部目录名，不包含私钥/数据。
平台程序独立于桌面升级包、不启动新服务、不改现有rc29或身份/可信钥。先在独立root所有测试目录用临时测试签名执行真实文件事务，不能称真实用户数据/硬件断电验收。拒绝错签名、重复/逃逸载荷、非root配置、软硬链接/可写父目录、换发布者/降级、原生及未提供能力；测试提交前各阶段中断恢复。保持最初失败日志，小差异备份。


### 2026-10-19 UBUNTUAPPS076平台安装事务
新增独立Ubuntu平台安装器，复用aitapp格式/验签，真实签名声明式安装及升级数据保留、初装/升级4阶段退出恢复与恢复再中断共22项主机检查通过。独立root临时目录，8次真实进程退出；测试用临时发布者钥不进入产品信任。Python3.8测试接口失败已保留。支持无权限schema1前置安装，原生架构准入、sandbox/storage、实际用户数据、平台登录接通与媒体仍未完成。未部署至生产、未修改rc29/冻结SDK/绑定，不重建未变的桌面。见docs/validation/ubuntuapps076-installer-20261019.json。

追加最终结果：拒绝布尔schema后23项通过；在/run独立root目录经受控CLI实际安装已签名测试包，再由UID1000的真实LVGL读取、选择、重开页面通过（800x480截图已看）。临时测试钥和安装目录清理，无生产变动。仅增量重编译UI测试夹具，未重建未变桌面产品。完整原生运行/storage/实际数据目标继续保留。

门禁记录修正：0255a96首次检查发现not_run项漏写scope，真实错误scope must be nonempty text已保留。补充完整未验收范围，代码不变，不重新编译。

安装器复核新增失败：签名文件清单不覆盖tar空目录，额外空data目录可被接受而沿用0755，破坏新data只对broker可见的初始权限。已独立回归复现并留reserved-data-before.log；将在解包前拒绝整个保留data路径，实际创建后再显式0700。此修复只影响新平台安装器，需重跑事务及真实签名页面链路，不重编译桌面。

空data目录准入修复后最终24项安装/恢复回归及受控签名安装→UID1000实际LVGL链路重验通过；原23项和首次复现失败保留。完整原生runtime/storage和生产集成未完。


## Ubuntu原生执行隔离：实施前（2026-10-19）
基准3c47e52。独立平台安装器已能安装无权限声明式包，但原生格式仍AArch64，Ubuntu执行/storage未接通。本批先落实Ubuntu平台侧独立x86_64执行器和真实本机程序拒绝测试，不复制SDK私有appd、不改旧schema2准入或公开冻结锁。完整目标仍包含签名原生目标/ABI准入、broker与真实数据跨升级，执行器单独通过不能称完整支持。
执行器只接受受控broker传入的root所有、不可被普通用户写入的常规ELF描述符，先检查x86_64 ELF基础头，不作为完整ABI/依赖验证。清理环境与多余FD、清除附加组、降至65534、资源上限、父进程退出跟随终止、no_new_privs与seccomp必须全部成功才执行。使用保留FD3执行原始程序，禁止关闭/替换它及执行其他文件；允许只读动态加载、内存/时间/信号基本调用和Unix stream客户端，拒绝文件写/进程创建/Internet/socketpair/设备ioctl/挂载等。这延续管理员批准发布者信任模型，不称容器隔离；Unix客户端需后续broker按真实PID/UID/父进程/文件身份重新认证。
验收：实际gcc编译Ubuntu程序和公共App SDK，真实执行正常回复、实际Unix请求、读取私有文件拒绝、文件写/联网/fork/再执行替换拒绝、FD泄漏拒绝、CPU超时及错误架构/非root入口失败。不启用安装器原生许可，不部署生产服务，不改原信任、rc29、A133；测试临时受控目录和独立进程，保存原失败与构建/测试哈希。

### 原生执行器修复与实际结果（2026-10-19）
原FD白名单不能约束execveat的路径内容：绝对路径可忽略fd参数，已由实际/bin/true执行复现（absolute-exec-before.log）。旧24项通过因此不代表安全结论。改为root监督进程只放行可信单线程启动桩的首次exec通知；进入原生程序后所有exec请求均拒绝，包括原FD自执行。listener经保留FD4单向交接，监督端随后关闭；应用不能重连/替换此描述符、改变过滤器或向其他FD发送权利。首次CONTINUE仅发生在任何不可信代码/线程执行之前，不依赖读取应用可修改字符串。
实际32项测试passed，额外父进程链检查passed：真实公共SDK Unix传输、内核UID/GID/组/能力/限制读回、写入/私有读取/直接Internet/fork/ioctl/再执行/绝对路径/过滤替换/FD泄漏拒绝，CPU或独立5秒墙钟终止，父进程退出后监督及native均终止。stdout必须为pipe，避免继承root已开可写文件。源码以-Wall -Wextra -Werror实际编译；完整安装器/ABI/storage/profile尚未接通，不能称原生扩展可用。
失败历史全部保留：/run noexec拒绝（改测试目录、不改mount）；过滤跳转误放行；收紧后动态加载pread64被拒；父退出测试读取/proc竞态；execveat绝对路径漏洞；CPU测试只期望CPU信号而先到墙钟期限（补内核CPU限制断言并接受两种受限终止）。最后32项与额外链路通过见docs/validation/ubuntuapps076-native-runner-20261019.json。
边界：65534共享UID、只读世界可读文件及Unix stream按系统DAC仍可访问；此原语不宣称文件/IPC命名空间隔离。完整签名目标准入、真实broker身份/权限及storage、响应大小限制、产品部署、更新/回退完整用户数据仍not_run。安装器继续拒绝native，未改rc29、生产身份/可信钥/登录服务或A133，未重建不变的桌面。

## Ubuntu原生目标格式与兼容性：实施前（2026-10-19）
基准23c456c。现有schema2固定AArch64，不改成按本机猜测。新增schema3，要求明确Ubuntu/x86_64/glibc/API1/独立native profile；旧1/2含义和拒绝规则保留。公共打包工具新增Ubuntu原生构建/验证入口，复用已有签名与载荷白名单，不执行待验证程序或ldd。检查ELF程序头/动态表的真实机器、解释器、依赖、版本及未定义符号，与实际受控宿主库匹配；拒绝私带加载器/库、额外搜索路径、错架构/运行时、未知依赖和缺失符号。不将清单声称当作ABI证据。
本批实际构建公共SDK程序、签名生成包、独立验证签名及宿主兼容，另测错目标/错误ELF/依赖篡改/缺库/缺符号及旧格式回归。此为安装前置，Ubuntu安装器继续拒绝native及storage权限，下一批完成认证broker/storage后接通。共享UI及已签rc29不变；源码、真实运行库证据和失败日志单独保存，不重建未变桌面、不改产品可信钥、不操作A133。临时测试钥不入产品信任。目录output/ubuntu-native-admission-076-20261019。

### Ubuntu原生格式实际结果
新增schema3明确Ubuntu/x86_64/glibc/API1/native-v1目标；原schema2继续只接受AArch64。init --native-ubuntu、compile-ubuntu、签名build及verify-ubuntu真实执行通过。ELF检查读取程序/动态表、依赖及符号版本/强未定义符号，与受控宿主loader/cache/递归库匹配；不运行候选或ldd，拒绝额外搜索路径/可执行栈等。构建receipt绑定实际源码SDK和宿主库SHA。完整ABI范围仅为文档列明的首个profile，未声明静态ELF/其他库和平台支持。
23项新测试passed，追加实际从签名包解出二进制并核对其SHA后经受限runner执行passed；24项安装事务回归和6项语言资源回归passed。schema1签名/验证及schema2拒绝真实x86_64保留。首次测试夹具把不存在路径当不可信路径造成FileNotFoundError；布尔schema提前被共享parser拒绝导致旧测试异常类别不符，两项失败留证后修正为真实可写父目录/更早的明确拒绝。未更改签名或安装准入来绕过测试。
安装器仍拒绝所有native和未提供权限，不宣称已完成应用安装/数据服务/更新回退验收。未重建共享桌面、未部署生产、未改rc29/身份/可信钥或A133。下一步认证broker与storage.app共享安装锁，真实签名程序通过SDK写读数据后再接通平台profile。证据docs/validation/ubuntuapps076-native-admission-20261019.json；原始output/ubuntu-native-admission-076-20261019。

复核发现：Ubuntu安装器在拒绝schema3之前会调用新的可选解析模块，给尚未开放的格式增加了不必要的特权依赖；隔离测试已复现。修复前追加：在公共格式解析前明确拒绝schema2/3，保留非法schema的原拒绝。后续真正开放native时必须验证整个部署依赖的受控所有权，不可只校验入口脚本。新增失败native-parser-before.log保留。

2026-10-20接续结果：提前拒绝修复后23项native及24项installer回归通过，可选native模块未被加载。611f2b1三项远端CI全部success，该CI属于修复前提交；修复后提交另行核对。原失败保留，生产/SDK/信任不变。

接续记录失败：80c18a3修改了已提交批次，追溯器拒绝Historical batches are immutable。恢复原批次字节，新增20261020批次承载修复；原生/安装代码与已通过测试不变，不重复构建。
