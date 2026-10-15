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

## 实现与首轮结果
system_image统一安全版本名/确定性gzip、原源前后检查/解压限额读回与禁止覆盖；build_release仅未来原启动链门禁后的归档输出改为产品名，BUILD-MANIFEST另外固定名/raw SHA/raw bytes/archive SHA。system_record可读取新压缩构建输入并核对该外部钉住的manifest/回执，也可将历史raw构建记录生成新名；prepare_release只生成规范image_asset，release_gate/构建来源/远端下载同名校验，历史无字段旧锁只读仍可验证。普通应用与集合原边界不改变；当前集合只支持application，未虚构系统多镜像集合支持。
已执行真实未来归档段的主机夹具：只输出SmartDesktopMimi-SystemImage-v9.8.7-rc.1.img.gz，raw9000字节/80字节gzip，原raw未改，明确not_run物理；不是完整SDK镜像。44针对性项passed，含真实Go共享checker+RSA签名的新名系统候选准备/验签、旧锁读取、错误版本/旧名新计划拒绝、双名/路径/覆盖/输入变化/限额、确定性和工厂/SDK关联。首轮tests模块路径缺失、次轮Go PATH缺失、旧签名夹具仍用旧名而被新生成器拒绝均保留日志；补齐执行环境和更新夹具到新格式后通过，未放宽命名规则。一次脚本尝试重排不存在的Path import而assert中止，其前序改动保留并已核查，后续正确重排通过。
全更新回归/架构检查待固定当前代码后运行，实际新SDK系统构建/设备/公开发布均not_run；冻结618/cdf/c22不动，未重签或更换可信钥。

整段933a214以来追溯初检发现实施前新增索引未列入实施批paths，检查如实failed；追加独立批覆盖索引与本说明，不修改历史批或放宽检查。

最终本批：代码49b6f19，184完整更新+74架构项0skip passed，公开源审计0发现；六项SDK配置/冻结raw/SWU/app SHA全部不变。证据verified-result.json绑定实际测试提交1aad9bb。新完整SDK镜像、物理及正式公开仍not_run；旧25附件不变。
