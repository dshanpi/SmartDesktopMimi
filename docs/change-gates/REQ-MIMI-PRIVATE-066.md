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
