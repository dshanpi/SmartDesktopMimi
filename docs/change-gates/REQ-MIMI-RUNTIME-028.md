# REQ-MIMI-RUNTIME-028 Ubuntu应用运行依赖基线

2026-10-14 实施前记录，承接ENROLL027/SDK025/SESSION026；基准f184064e396132784358800918011bc195d712bc。UPDATE001/TRACE001保持。

## 背景、流程和失败分支
已有真实主机基础登记没有完整应用SONAME版本，不能签包后跳过运行库检查。本轮读取固定Ubuntu平台依赖起点的ELF DT_NEEDED闭包，检查实际x86_64共享库、root控制的固定库路径及dpkg归属/完整包版本，逐文件固定SHA256，形成运行库摘要能力。登记检查每次重读并比对；缺库、路径逃逸、错误架构、无安装包归属、版本不识别或摘要不符拒绝。签名应用要求相同库版本范围及完整摘要，不凭归一化版本相同放行不同发行包字节。

## 不变量与范围
仅Ubuntu20.04 VMware开发主机档案，不改A133；不执行被检查库、不用ldd执行任意候选；不复制主机库到应用包，不改系统库。固定库搜索根，仅读取系统root所有且不可普通用户写入的文件。SONAME版本使用dpkg上游数字版本归一化，完整dpkg版本/实际路径/内容哈希进入摘要；这不是通用Debian兼容性承诺。动态插件和全部外设能力不能由DT_NEEDED闭包证明。既有签名密钥保留，公钥配置不随应用自授信。

## 操作与验收
实现位置services/mimi-update/update/ubuntu_runtime.go和Ubuntu inspect/登记分支、相关测试和说明。先主机回归和实际读取/性能验证，再固定源码编译并生成新版独立root运行登记（保留ENROLL027旧记录）。若输入满足，再使用原有产品签名验证本地rc19应用和amd64 deb；纯传输包安装不等于激活，可信平台会话钩子与事务验收仍待后续。新产物绑定rc19原应用构建源及本轮平台工具源。源码/证据仅关键增量备份。

- not_run：库闭包/版本/目标拒绝测试、实际主机采集与登记、签名包和deb读回复验。
- not_run：Ubuntu可信启动/实际升级回退/数据及断电，A133完整系统验收。
- blocked：A133失联根因未解决；Tuya真实云设备仍待提供。

证据output/ubuntu-runtime-20261014/。完成后更新状态及哈希，不覆盖旧登记/旧失败；不上传。

首次实际只读探测已得到86个ELF库，但核查发现Debian字母后缀1.1.1f被正则回退归一化为1.1.0，判failed，证据runtime-r1.json保留。修复为完整合法字符校验与数字前缀提取分开，新增具体OpenSSL版本回归；未使用错误探测结果登记/签包。

实际运行登记passed（约0.45秒，低于现有5秒Provider身份预算），旧基础档案拒绝passed。rc19固定应用签包与amd64 deb构建passed，下一步在主机安装此仅传输包，确认dpkg无同名已装包/目标目录不存在、包无维护脚本及root路径范围正确后才操作；安装后对真实/usr/share负载重新验签，再仅卸载本次新包，核对已有会话配置数据未变。此流程不启动服务、不激活桌面、不改可信密钥/既有应用版本，不冒充升级事务验收。

## 2026-10-14 固定产物与实际主机验收
平台工具源码2662a5b291c6e629bcc4922e68883554cf4b577b，原生updater SHA256 f12a9cf9a4f23e40fba94fd6599d8e1c0aa9abfda07c56518fe76a07d76562ec，A133交叉updater 99f78c5d95019acde133c04fcfdf53ca681cf7ac024d14b890981dda245c82ae，均编译passed；A133未安装。实际86个库的版本/ELF/归属/内容闭包检查passed，runtime-bundle-sha256:477dbdd15cb2b29b92765c90adefc84226004c53f4ed060aaab75a79ae55fc7f。修复后的libssl/libcrypto归一化1.1.1正确；r1错误保留。

新的独立root登记/var/lib/smartdesktopmimi-platform/ubuntu-runtime-20261014检查passed（0.45秒）；ENROLL027旧硬件档案因缺运行依赖被拒绝passed，旧档案保留。文件内容摘要不变而发行包版本改变也改变摘要能力，不能只凭粗粒度SemVer跨发行版。

固定应用仍为rc19 / e5b7186529ec574314a0de5e8e21b49b301d7456，签包前逐一核对完整构建树哈希，未混入本轮平台代码。沿用现有产品私钥签名，本地应用包SHA256 e24190c6faf4d376e2e8821e66184423cf13f962eaf981e916effb22b8e9ac19，amd64 deb SHA256 d38a3ac032d88c965de165cdeac0a65a1590c5cb676000c4e87540c17f546150。打包/目标/签名验证passed。

实际dpkg传输包验收passed：检查12个归档条目root所有/白名单路径/无维护脚本，确认无既有同名包后安装，核对installed版本1.0.8~rc.19-1及amd64，/usr/share安装副本三项逐字节与签名包一致，重新验签通过，再purge本次包；原会话两个数据文件哈希不变。错误平台/库版本/库摘要/篡改签名四类拒绝passed。没有安装或激活桌面服务，没有更新器事务、物理显示、应用升级/回退或断电数据验收。

证据output/ubuntu-runtime-20261014/result.json、registration.json、deb-acceptance.json、runtime-r1/r2.json及对应日志。下一步为root平台会话管理与更新器钩子（身份/空闲/停止/启动/真实健康）集成，之后实际签名激活及事务矩阵；不得将此次dpkg状态记成应用激活状态。应用/Release无上传。

收尾回归：Go整组、116项更新、74项架构、63项SDK/服务测试passed；实际提交差异覆盖检查在本次证据提交后执行。
