# REQ-MIMI-DEBACTIVATE-030 已安装deb载荷的真实激活闭环

2026-10-14实施前记录，承接SDK025/RUNTIME028/ACTIVATE029。基准45ec5d55a636fb21cdab9d6b27bcf9604fa836a7。用户已指定Ubuntu开发主机为deb首验收目标，A133保持Tina；应用/Release上传约束不变。

流程：新干净源码rc22与固定SDK完整构建→原产品密钥签名→匹配真实主机身份/86库基线生成amd64 deb→核对dpkg元数据和无安装脚本→安装仅传输包→验签已安装载荷及源/包哈希→可信平台更新器从已安装路径执行真实升级→连续真实UI/Provider健康后提交→删除deb传输包后桌面与用户设置仍保留→停止并重新验签启动当前版本→清理专用显示与会话。任何签名/来源/平台/运行依赖失败均拒绝，不用dpkg状态代替激活状态。

回归不变量：不通过deb替换平台Provider/公钥/硬件身份，不写普通用户主桌面自启动；沿用029专用root运行基线及同一用户数据；用户非root运行；启动必须重新校验。rc21为故障验收失败包，保持failed/high_water并拒绝重放；rc22使用独立真实构建回执，禁止重标旧包。HDMI关闭，不触碰A133或其他硬件任务。只备份关键来源及证据。

本批源码变更范围VERSION及本门禁/索引/不可变批次。产品业务UI不变；通过构建工具和现有更新器完成实际事务。证据output/ubuntu-deb-activation-20261014/；not_run：新构建/签名deb安装载荷激活与purge后重启。not_run：物理面板、图形登录开机启动、整机断电/系统矩阵；blocked：A133通道当前busy(其他COM18串口)，不操作该硬件；Tuya资源待提供。通过后只声明Ubuntu开发主机本次限定链路，整套发布未完成。

## 2026-10-14 实际结果
rc22干净源码57f1b6559d0d4e429143ad49726db19f6b617070，固定SDK d92c1a17…d1740完整应用构建通过；签名应用616065f1acfe1f75f0672e9502056de5e2b88dc5b728a81876b0242c8cc3d06e，amd64 deb 9daeff49a71056b3b1f44d7b9407a85e7a26a50bbfe3d1573b542005e692b711。平台沿用ACTIVATE029及原可信公钥。

passed：18条实际阶段记录覆盖打包、无maintainer脚本、dpkg安装/版本核对、安装载荷逐字节比对与验签、安装路径实际升级、连续健康、purge后健康、停止/重新选定启动及健康、最终清理。current rc22/previous rc20/high_water22/trial=false；rc21失败记录保持。用户设置1文件逐字节保持，平台Updater/Provider/session/公钥/设备登记/配置八文件哈希不变。实际SDL截图为approved首页；Xvfb不等于实体面板。两专用单元最终inactive/MainPID0，X110 socket消失。

构建回执activation=not_run保持构建时原始事实；实际激活证据在output/ubuntu-deb-activation-20261014/result.json，不改写构建回执。本批业务代码未改，029主机回归仍绑定其实现，不声称030重复运行全部测试。

not_run：图形登录/开机启动产品化、完整用户数据/迁移、物理屏、整机断电和系统矩阵。A133本轮再次失联后按授权恢复原rc17，根因未解决；Tuya云项目设备待提供。无应用/Release上传，整体交付未完成。下一步A133稳定性/系统矩阵及Ubuntu登录集成。
