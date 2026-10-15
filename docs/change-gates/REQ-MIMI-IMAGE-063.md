# REQ-MIMI-IMAGE-063 产品名系统镜像输出与发布链
2026-10-15 实施前记录，落实EDITION062文件名要求；关联UPDATE001、DELIVERY058、MIMI-TRACE-001。

## 范围与流程
未来系统构建/记录/发布统一生成SmartDesktopMimi-SystemImage-v<独立系统版本>.img.gz，A133仅留在SDK输入路径/签名身份和系统标签内部。原rc2/rc23及25附件不重命名覆盖，不重建旧产物。
先严格校验版本并产生唯一安全文件名；从固定原raw生成确定性gzip，完整解压后SHA/大小及源前后不变检查，通过才生成可见产物/回执。未来build_release在原启动链门禁后调用；system_record同时支持历史raw构建记录和新产品名压缩构建输入，固定原manifest/checksum/SDK/factory关系；prepare_release仅生成新命名锁，release_gate/来源检查/集合/远端复核按锁中受控image_asset执行。历史锁无字段仅允许旧system.img.gz读取，新名字/版本不符、两种名字并存、输入变化或覆盖现有输出均拒绝。

## 不变量与失败
显示命名不改变平台、板型、签名、镜像字节、独立版本/标签或安装校验；不是通用镜像声明。image_asset必须由版本计算，不允许任意路径。普通应用命名不变。历史读取兼容不能成为新生成器输出旧名字的默认通道。所有失败保留，不把压缩/测试算真机通过。

## 实现及验证
计划tools/update/system_image.py、system_record.py、prepare_release.py、release_gate.py、build_provenance.py、github_release.py及scripts/build_release.sh，现有系统/准备/发布/集合回归和独立压缩/篡改/名字拒绝测试。
源码起点933a214，证据output/system-image-name-20261015；主机夹具不是真实镜像。实际新完整SDK镜像、设备安装、正式公开发布均not_run，A133物理按用户最新要求后置。当前实际实现/回归待执行；完成后绑定代码、测试、版本和产物回执。
