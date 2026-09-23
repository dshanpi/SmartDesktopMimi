# Submodule

本产品仓库用 Git submodule 管理应与产品自有改动保持分离的上游代码：

- `apps/lv_port_linux/lvgl`: 产品 LVGL fork（submodule）。

clone 本产品仓库后初始化：

```sh
git submodule update --init --recursive
```

submodule URL 记录在产品仓库根 `.gitmodules`。`apps/lv_port_linux/.gitmodules` 仍保留，
因为 `lv_port_linux` 本身也来自独立项目布局。

## 与构建的关系

- `lvgl` submodule 直接参与 `lv_port_linux` 的 out-of-tree 构建（`add_subdirectory(lvgl)`），
  必须初始化。
- `third_party/TuyaOpen` **不是 submodule**，而是 **vendored 源码**（主仓 + platform/LINUX +
  `.tools`，已删 `.git` 历史，离线可编）。构建用的可写副本默认放在 `.tuyaopen-build/`，
  由 `scripts/prepare_tuyaopen_build_root.sh` 从 vendored 源 rsync 生成。详见 [tuyaopen.md](tuyaopen.md)。
