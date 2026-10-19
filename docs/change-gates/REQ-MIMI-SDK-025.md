# REQ-MIMI-SDK-025 Ubuntu x86_64 SDK 与 deb 架构配对

2026-10-14 实施前记录；关联BUILD-017、UBUNTU-022/024、MIMI-UPDATE-001和TRACE-001。基准8def02089958c45b2c3fe3df63a33775f96b6541。

## 背景与范围
用户指定Ubuntu开发主机作为首个deb验收目标，A133保持Tina。现有SDK/receipt/profile/deb仅支持a133+aarch64，原生开发编译记录不能冒充正式SDK。新增精确组合ubuntu-host+x86_64+ubuntu（schema2），保留A133既有组合；Ubuntu系统版本由SDK内os-release验证，不将仅架构相同当兼容。需要实际SDK输入锁、编译器目标校验、独立应用编译和amd64传输包，平台服务仍不进入应用包。

## 流程与失败分支
选平台/SDK外部哈希→核对锁、平台/架构/OS/工具链→依赖链接检查→独立桌面编译→核对源码/SDK未变化→构建记录→签名包验证→映射deb架构。任一步缺失或不匹配则停止，不默认A133、不跳过签名、不使用宿主头文件补缺。配置选择只在开发主机。

## 不变量
A133 SDK schema1与原有schema2行为兼容；T527/RK3576仍未接入；ubuntu-host仅在x86_64 Linux构建主机编译，不能接受ARM工具链。纯应用不包含Provider/信任/设备身份。原签名、公钥、安装目标匹配与发布限制保留。开发SDK可构建不等于可信设备登记或通过安装验收；明确区分真实构建与夹具deb。

## 实现与验收计划
tools/update/application_sdk.py、application_build.py、cmake/application-sdk.cmake、tools/sdkbuild/configure.py和package_deb.py，以及SDK/构建/错配测试。实际SDK捕获材料留.delivery，只保存输入和关键差异，不复制整个主机/SDK缓存。公开许可与信任基线另行验收。
- not_run：Ubuntu精确目标通过、混搭平台/OS/架构/前缀拒绝、A133原回归。
- not_run：实际Ubuntu SDK输入捕获、固定输入编译与运行。
- not_run：真实签名amd64 deb安装/激活/升级/回退/数据，不以fixture冒充。
- blocked：A133失联/系统验收与整体发布，沿用原记录。

## 接续
本轮先打通可信构建输入和架构配对，再接平台启动/健康与签名安装。证据output/ubuntu-sdk-20261014/；实际提交和哈希完成后追加。全部必需验收通过前不上传。

### 实施范围补充（捕获工具实现前）
新增tools/sdkbuild/capture_ubuntu_sdk.py：仅从当前Ubuntu x86_64主机已安装的明确开发依赖及其已安装依赖闭包捕获构建输入，限制到头文件/库/pkg-config、GCC工具链和os-release；不复制/etc、家目录、设备文件或凭据。保存包版本、来源路径与实际输入哈希，链接必须留在SDK边界内；不修改宿主软件。它是受控主机构建输入快照，不声明离线全系统复现或公开分发许可合格；编译器宿主运行依赖也要记录并验证。只创建新目录，失败不生成可用SDK锁。

## 提交前回归
passed：SDK/服务54项（sdk-tests-r1.log），新增Ubuntu目标/前缀/OS拒绝及真实amd64 deb档案检查；此deb使用明确fixture verifier，不是签名安装。后续环境隔离/架构记录修改后重跑原15项和新增7项均passed。架构74项passed。更新/发布工具首轮因PATH未包含既有Go失败（update-tests.log），补构建PATH后116项passed（update-tests-r2.log），未改产品逻辑绕过。冻结V1和公开源码审计通过。
捕获预检最初遇到dpkg虚拟依赖awk，改为解析已安装Provides闭包；不安装新包。预检所需空间小于1GB，主机可用45GB。限制捕获头文件、链接库/对象/pkg-config、GCC内部输入和os-release，排除普通服务配置/可执行文件；编译器宿主运行库锁定并由verify_sdk复核。实际捕获和新SDK编译仍待固定源码后执行。

### 实际捕获失败与修复计划（先记后修）
3397882首次捕获failed，capture.log：GCC的libstdc++.so链接目标不在已选输入中。调查发现主机同时安装amd64/i386的同名libstdc++6，依赖索引按去架构包名覆盖，误选i386；不能跳过链接/清单检查。下一步将捕获严格限定amd64/all，新增多架构同名包和虚拟依赖回归，再生成新输入。原目标未生成，临时材料自动清理，未修改主机。

多架构修复回归passed：ubuntu-sdk-r2.log 10项（新增amd64/i386同名不覆盖、只有错误架构不得回退、虚拟/候选依赖与循环解析）。实际新捕获和构建not_run，接续本轮。

## 实际固定输入构建结果
源码 `4674523552fac6c997748454ceb342d916a581a7`；SDK SHA256 `d92c1a17ea83af45f3ca6f04678b6d893237d74873f8e76430dec251519d1740`。捕获passed：4280个输入，源文件822933428字节，仅一份SDK保存在.delivery/application-sdk-ubuntu-x86_64；不重复大备份。来源为已安装包实际文件，不等于发行版官方包字节证明；公开许可审计not_run。
Ubuntu完整应用构建passed（桌面、Web、资源树、SBOM、构建后SDK与源码复核）；桌面SHA256 `dfbfa7ce52b38e61468287db328099b8cec3e2d987e4fa2a2cc9cc39d75d849f`，receipt为output/ubuntu-sdk-20261014/ubuntu-application-build.json。真实SDK链接预检passed；原生窗体验收三分辨率/未编译分辨率拒绝/V2查询/退出清理passed，实际配对截图正确；Xvfb不代替实体屏。
A133 SDK `bb78f59b8a4b2128123d5601fc782bc5360c9be567ecd4ad3979f1b000482261` 回归编译passed，桌面SHA256 `1abae4491a05b15245cfe483aa6461462894ef69cfb59273d9b394ba6a32a269` 与前序rc18相同；不安装到设备、不把旧包当成本轮新包。
实际签名amd64 deb、Ubuntu可信登记/平台会话启动/激活/升级回退数据仍not_run；现有amd64 deb测试是夹具归档验证。全发布未完成、未上传。
A133通道空闲后本轮读健康再次failed（串口空两次+ping超时），按STABILITY-023已有授权恢复，实际确认原rc17/current34fc809a/previousrc16/trialfalse/health0，内存可用约1.8GB、ext4p7错误0、恢复后温度50.210摄氏度、频率1512000kHz。首条命令在getty就绪前not_run，第二条实际执行；serial-1已关闭、无临时SSH。根因仍未定，不以启动后读数排除故障原因或宣称稳定性通过。

## 2026-10-19 当前Ubuntu产品缺口（构建前）
原Ubuntu rc22早于当前UI075组件/设置代码；本轮先按锁定SDK d92c1a17ea83af45f3ca6f04678b6d893237d74873f8e76430dec251519d1740构建当前完整应用（desktop/Web/receipt），支持既有三分辨率与可选voice编译，runtime voice仍默认关闭。需要重建是应用输入自rc22已有变化，不是因文档变更。不重建A133、不改原运行rc22、不签名或复用旧版本发布标签。
先执行原SDK/来源/契约守卫，失败保留日志并按实际原因修复；构建后逐文件复核。后续另做设置/布局兼容及受控升级。完整数据目前不能判通过：已知平台仅设置文件和后续widgets，扩展应用仍板端路径，Ubuntu Provider没有媒体/扩展能力。禁止用无消费者的样例文件替代功能验收。事前证据output/ubuntu-current-app-20261019/intent.json；真实升级/退出再登录/完整数据均not_run。
