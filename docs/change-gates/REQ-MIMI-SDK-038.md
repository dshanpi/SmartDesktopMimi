# REQ-MIMI-SDK-038 空输出SDK的64位工具链初始化

2026-10-14实施前；SDK032真实构建在IPKVM视频编译缺A133 aarch64 g++失败；新OpenWrt依赖、桌面/HDMI/平台服务及Web构建已按实际日志推进，不能把总体失败称镜像成功。97组件tracked HEAD/diff与8启动链输入最终不变，旧job已terminal，036无缓存kernel源已守卫激活、旧树保留。

已保存prebuilt/kernelbuilt/aarch64组件commit b62bd9ff60730499cd6eace4d727a54c547a6334，gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu.tar.xz字节179961868/hash1941dcf6229d6706bcb89b7976d5d43d170efdd17c27d5fe1738e7ecf22adc37、Gitblob7f8d23fba81bb55a4847e703f9ba3fc22a89245d实际hash-object一致。不是缺来源或下载新编译器，而是清out后尚未执行SDK prepare_toolchain；OpenWrt32位工具链不能替代视频64位工具链。

流程：固定存档与.config/.buildconfig/97输入/8保护文件→确认旧构建PID消失、canonical内核无vmlinux/o/generated且36receipt有效→只执行受限SDK ./build.sh kernel（官方prepare_toolchain展开保存编译器并真实全内核构建，不调bootloader/Linux整构建）→核对内核实际完成、编译器可执行/版本/身份、保护文件及源变更→复用本轮已有OpenWrt新依赖，重跑产品应用/完整镜像/签名门禁。独立log/returncode/实际输入来源记录，不覆盖032失败，不重复恢复全部SDK。

产品build_apps增加早期明确检查A133视频所需64位gcc/g++/ar/ld，缺时在清应用/长依赖构建前拒绝并给出只编译kernel的初始化入口；不得换用PATH中其他编译器、默认为兼容CPU、进入桌面UI或自动重建启动链。仅检查SDK依赖、无需新桌面版本；验证实际缺项拒绝、初始化后实际A133完整构建，Ubuntu无该入口不误调用。当前kernel全编译/编译器初始化/后续产品均not_run，整体交付不合格。现有可信钥/设备绑定不变，HDMI off。

早期工具链检查已实现scripts/build_apps.sh，在清应用/长staging构建前检查固定64位gcc/g++/ar/ld；缺项直接给出受限kernel初始化入口，不尝试其他编译器或执行fullSDK。真实CLI缺编译器用例passed（--clean仍保留预置sentinel）、已初始化的实际A133工具链同一检查passed，bash -n通过。SDK kernel-only job313025/313026已从已激活无cache树运行，编译器从固定保存archive初始化；完整kernel/保护文件/source/config核对仍等最终receipt。
