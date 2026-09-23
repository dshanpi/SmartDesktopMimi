# AI-DeskTop-Box LVGL 图形库（v9.5）

本仓库是为 **AI-DeskTopBox** 项目（[100askTeam/AI-DeskTopBox](https://github.com/100askTeam/AI-DeskTopBox)）专门定制的 LVGL（Light and Versatile Graphics Library）图形库。它基于官方 LVGL v9.5 代码，针对 AI-DeskTopBox 的运行环境进行了适配和配置。

## 仓库说明

- **基础版本**：LVGL v9.5
- **所属项目**：[100askTeam/AI-DeskTopBox](https://github.com/100askTeam/AI-DeskTopBox)
- **仓库类型**：私有（Private）

本仓库作为父项目的 **git 子模块** 使用，为桌面系统提供图形界面底层能力。

## 使用方法

在克隆父项目时，需要同时拉取本子模块：

```bash
# 方式一：递归克隆父项目（推荐）
git clone --recursive git@github.com:100askTeam/AI-DeskTopBox.git

# 方式二：如果已克隆父项目，再初始化子模块
cd AI-DeskTopBox
git submodule update --init lvgl
