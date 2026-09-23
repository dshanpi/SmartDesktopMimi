# Allwinner G2D 接入 LVGL9（状态：移植完成，待内核驱动修通后启用）

> 本文档记录 G2D 硬件加速接入 LVGL9 的移植过程、设备实测结论与启用步骤。读者：UI/显示驱动开发。

## 结论先行

G2D 硬件加速接入 LVGL9 的**移植代码已完成，且上机逐段验证全链路正确**（Phase 1：不透明矩形填充）。这不是占位代码或半成品——**当前未启用的唯一原因**是该设备固件的 **G2D 内核驱动不工作**（命令提交后完成 IRQ 永不触发，根因见下"BSP/内核根因"一节），与移植代码无关。内核/DT 修通后，把 CMake flag 改回 ON 即可启用。

- 移植代码完整保留在仓库中（`apps/lv_port_linux/src/draw/sunxi_g2d/`），随时可用；CMake 选项 `LV_USE_SUNXI_G2D` **默认 OFF**，不编译、不链接 libuapi，lvglsim 行为与无 G2D 时完全一致（纯软件渲染）。
- 启用动作只有一个：G2D 硬件/驱动修通后，把 `LV_USE_SUNXI_G2D` 改回 ON 并清理调试日志（详见文末"启用步骤"）。

## 移植架构（v9 draw unit + ION 内存桥接）

不能照搬 SDK 里 LVGL v8 的 g2d demo（hook `lv_draw_ctx_t`，v9 已无）。改为 v9 draw unit 模型，结构照 fork 内 `lvgl/src/draw/nxp/g2d/`（v9 模板），底层换成全志 ION + `/dev/g2d` ioctl。

- **自定义 draw buf handlers**（`lv_draw_buf_get_handlers()` 覆盖 `buf_malloc_cb`/`buf_free_cb`/`invalidate_cache_cb`）：让 LVGL 的 `lv_draw_buf_t` 走 ION 内存（失败回退 malloc）并登记进 buf_map。
- **v9 draw unit**（`lv_draw_create_unit` + evaluate/dispatch/delete_cb）：evaluate 仅在目标 draw_buf 命中 buf_map + RGB565/ARGB8888 + 不透明 + 无圆角无渐变 + 面积≥12100 时认领 FILL；与内置 `lv_draw_sw` 单元共存，其余任务回退 SW。
- **G2D 调用**：`G2D_CMD_FILLRECT_H` + dma_buf fd 模式（`use_phy_addr=0` + `fd=SunxiMemGetBufferFd`），area 映射照 NXP，ioctl 填法照 v8 `sunxig2d.c`。
- **cache 协议**：ION（libuapi `SunxiMemOpen`/`Palloc`/`FlushCache`/`GetPhysicAddressCpu`），cached；g2d 前 flush、flush_cb memcpy 前再 flush。

上机逐段验证：init 成功 → 内存桥接通（display draw buf 是 ION 且入 buf_map）→ evaluate 正确 tag 大块不透明 ARGB8888 fill → dispatch 正确认领 → `fill_rect` 参数正确调用 ioctl。**全链路正确，卡在 ioctl 本身**。

## 设备实测结论（2026-07-07）

脱离 LVGL 的极简复现（仅 `SunxiMemPalloc` + `SunxiMemGetBufferFd` + `G2D_CMD_FILLRECT_H`）：

```
alloc ok: vaddr=0x7f8cc29000 phy=0xff600000 dma_buf_fd=6 size=1536000
ioctl ret=-1 errno=1 (Operation not permitted)
```

内核同步打印：
```
G2D irq pending flag timeout
G2D FILLRECTANGLE Failed!
```

`/proc/interrupts` 中 G2D IRQ 始终为 0（命令提交前后都不增长）：
```
371:  0  0  0  0  wakeupgen  91 Level  6480000.g2d
```

环境确认（都正常，排除这些因素）：
- 内核 `Linux 4.9.191 aarch64`，`/dev/g2d`、`/dev/ion` 存在
- G2D 时钟 `rate=300000000`（但 `enable_count=0 / prepare_count=13`，prepare/enable 不平衡，可疑）
- IOMMU：`6480000.g2d` 已挂到 iommu group 0，IOMMU basic DMA 测试 SUCCEEDED
- live DT：`compatible=allwinner,sunxi-g2d`、`status=okay`、`reg=0x06480000`、`interrupts=GIC_SPI 91`、`iommus=<&mmu_aw 5 1>`，均正确
- 原厂 `g2d_fill_dmabuf` sample 用旧 `ION_IOC_ALLOC`，在当前系统分配失败（ION ABI 不同），不能作为硬件判据

**最小复现 = ION/dma-buf 通、IOMMU 通、G2D 命令提交后硬件完成 IRQ 不来。**

## BSP/内核根因（最可能）

4.9 的 G2D 驱动（`kernel/linux-4.9/drivers/char/sunxi_g2d/g2d_driver.c`）**硬件初始化不全**：

- `g2d_probe` 只 `of_clk_get(node, 0)` 取**1 个时钟**、`clk_prepare_enable` 只开它，**全程没有 `reset_control`**；
- 但 A133 G2D 按 SDK 5.10 DTS（`device/config/chips/a133/.../linux-5.10/sun50iw10p1.dtsi`）需要 **3 个时钟 + 1 个总线复位**：
  ```
  clocks = <&ccu CLK_BUS_G2D>, <&ccu CLK_G2D>, <&ccu CLK_MBUS_G2D>;
  clock-names = "bus", "g2d", "mbus_g2d";
  resets = <&ccu RST_BUS_G2D>;
  ```
- 设备 live DT 只声明了 **1 个 clock phandle**。

因此 **BUS_G2D（寄存器访问）和/或 MBUS_G2D（DMA 访存）时钟没开、总线复位没 deassert**：写 `G2D_MIXER_CTL` 启动命令时硬件没真正执行，或 MBUS 没通导致 g2d DMA 挂在首次访存 → 命令永不完成 → 完成 IRQ 不来。`G2D_AHB_RESET` 只是 g2d 寄存器 bank 内部的软复位（`0x08+G2D_TOP`），不是 CCU 总线复位。

## 修复方向（内核 BSP 侧，非移植代码）

1. 改 4.9 G2D 驱动 `g2d_probe`：按名字取全 3 个时钟（`bus`/`g2d`/`mbus_g2d`）全部 `clk_prepare_enable`，并 `devm_reset_control_get` + `reset_control_deassert(RST_BUS_G2D)`。
2. 或修设备 4.9 的 G2D DTS：补齐 3 个时钟 + `resets` 属性，对齐 5.10 DTS。
3. 快速判活：G2D open 后读回 `G2D_MIXER_CTL`，若返回 `0xffffffff` 即总线时钟门控。
4. 修好后用上述极简复现验证：`FILLRECT_H` 返回 0 + `/proc/interrupts` G2D 计数递增 = 硬件通了。

## 启用步骤（G2D 硬件修通后）

1. `apps/lv_port_linux/CMakeLists.txt`：`option(LV_USE_SUNXI_G2D ... OFF)` 改回 `ON`。
2. 清理 `src/draw/sunxi_g2d/` 里的临时调试日志（evaluate/dispatch/exec/_fill/fill_rect 的 `fprintf`，保留或精简计数）。
3. 重新 `build_apps.sh` 验证 WERROR，上机确认大块纯色填充正确不花屏。
4. 推进 Phase 2（alpha fill/blit/image，src 也要 ION）、Phase 3（flush 用 g2d blit 到 fb、scale/rotate）。

## 移植关键坑（备忘）

- libuapi 用 `SunxiMemOpen`（`SunxiMemSetup` 是桩函数 `return -1`）。
- A133 G2D 有 IOMMU，必须用 dma_buf fd 模式（`use_phy_addr=0`+`fd`），`use_phy_addr=1` 直传物理地址会被拒。
- `LV_DRAW_BUF_STRIDE_ALIGN=1` 是前提：全志 `g2d_image_enh` 无 stride 字段、按 width*bpp 算行距，须与 LVGL 打包行距一致。
- display draw buffer 必须用 `lv_draw_buf_create`（→ 走自定义 allocator → 入 buf_map），否则 evaluate 全拒绝。
- 该工具链 `--sysroot` 不把 `staging_dir/target/usr/include` 加入 `<...>` 搜索，CMake 需显式 `target_include_directories(lvgl_linux SYSTEM PRIVATE ${SYSROOT_PATH}/usr/include)`。
- stdio `printf` 重定向到文件是块缓冲，关键日志用 `fprintf(stderr,...)`。
- 显示实际 32bpp ARGB8888（非 `lv_conf` 的 RGB565，sunxifb 运行时按 fb bpp 覆盖）。
