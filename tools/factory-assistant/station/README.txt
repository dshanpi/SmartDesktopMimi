AITVBOX 出厂助手 · 工位数据目录（工程包内，默认路径）

本目录随出厂助手工程/安装包管理，不使用 %LOCALAPPDATA% 或 ~/.local/share。

  keys/     放置 100ask_ecdsa.pem / 100ask_ecdsa.pub
  data/     tuya_licenses.sqlite（库存库）
  imports/  可选：涂鸦 xlsx
  logs/     可选：日志

覆盖：设置环境变量 AITVBOX_FACTORY_HOME 指向其他目录。
Windows 发布时请把本 station 文件夹与 exe 放在同一安装目录下，
或让安装程序创建 <安装目录>\station\。
