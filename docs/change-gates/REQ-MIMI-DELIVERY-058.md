# REQ-MIMI-DELIVERY-058 先同步源码与候选产物，真机验收后置
2026-10-15 用户最新明确授权，实施前记录。关联DELIVERY033、SPLIT057、MIMI-UPDATE-001、MIMI-TRACE-001。

## 背景与范围
用户要求先同步源码、A133 SDK overlay、镜像与应用升级包，明确A133失联定位、系统安装、升级/回退/断电验收暂不做，提供新SDK Ubuntu环境后集中重做。本次指示替代旧“真机通过前禁止一切上传”的当前阶段前置；不把未执行改为passed。上传统一仓库的受控草稿快照，不公开稳定Release、不修改设备安装门禁或可信密钥。源码和已有二进制各按真实提交绑定，不声称最新源码已生成旧包。

## 流程与失败分支
收尾当前SPLIT057受影响主机验证 → 检查公开源码/历史及附件敏感边界 → 固定已有rc2镜像/OTA与rc23应用包及原签名 → 同仓库独立标签对应实际源码，创建draft/prerelease快照 → 上传固定附件 → 逐项下载验签/哈希/版本关联复核 → 提供实际链接。缺失/篡改/错误类型/冲突标签或现有已公开Release一律拒绝，不覆盖历史。快照的delivery-snapshot.json明确非正式自动更新release-lock；设备发现仍忽略draft，不能绕过安装验证。后续正式发布沿用完整流程与必需验收。

## 回归不变量与实现
dshanpi/SmartDesktopMimi唯一应用/系统入口；100askTeam/A133-AIDesktop-Tina5私有标准overlay不变。秘密、私有SDK体/库不得进入公开源码包；不改原618/cdf/c22候选、不重签换钥。当前受控快照不宣称已验收或可量产。失败历史保留，下载复核按实际附件逐项记录。
实现：AGENTS.md、docs/application-update-policy.md、CURRENT_PROGRESS.md、本门禁/批次；一次性受控同步脚本置.delivery/内并保存脱敏证据output/delivery-sync-20261015，不修改常规release_gate/promote约束。待验收状态保存在快照及说明。源码推送/附件上传/远端复核目前not_run。

## 接续
完成此次同步后，等待新SDK Ubuntu环境再登记实际SDK输入/板卡并重新进行安装和各阶段升级、断电、数据及原功能验收。Ubuntu控制台当前不可进入、涂鸦资源未提供、真实语音/账号归属保持未验收。全目标尚未完成，不因本轮上传停止长期循环。

## 实际同步进度
应用main已fast-forward推送c81e23b及后续5ad62aa历史覆盖补记；应用v1.0.8-rc.23与系统system-a133-v1.0.0-rc.2轻量标签分别指向实际a37/d7，不移动历史标签。公开候选审计0finding及新增历史909blob已知秘密签名0finding passed。两个GitHub draft/prerelease已创建：应用ID413348355，系统实际ID见output/delivery-sync-20261015/system-remote-draft.json。应用12附件上传完成、下载进行中；系统13附件上传进行中。真实上传完成前不标远端复核passed。
初次同步脚本在旧cryptography backend参数、RSA签名API及OTA字段路径核对三处失败，保留prepare/r2/r3日志，修正后r4固定原RSA PKCS1v15 SHA256验签和raw/ota各自原哈希通过；未换钥/重签或改候选。为降低传输等待，严格核对自己传输PID后结束串行下载，保留不合格partial，改两类型/独立附件并行；不触及设备或其他任务。目录/目标/原SHA不变，全部远端文件仍需完整下载复核。

## 同步最终结果
应用12/系统13附件已全部上传并逐个完整下载；远端asset ID/size/SHA、四原RSA签名及gzip解压raw618/应用c22/SWUcdf和a37/d7版本配对全部passed。系统SWU慢流采用固定asset ID的HTTP206范围读回及慢段再拆分，严格Content-Range/字节上限/允许CDN、最终全包原SHA后接续总复核；两次仅结束自己传输PID并保留partial，不把partial记通过，不触及设备。详见verified-result.json、system-swu-range-verified.json、upload-final.log及handoff记录。当前上传同步这一项passed，真机仍deferred/not_run，整体项目未完成。草稿入口及附件说明已保存docs/delivery/20261015-snapshot.md，未公开稳定版/未替换标签或签名钥。
