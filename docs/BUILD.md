# 从公开源码构建 0.8.0

本仓库可以编译原生 DLL，包含它对应的 `src/RuntimeForms.h`。重新生成 ESP 需要五个原始上游插件，见 [数据构建器](../data-builder/README.md)。本仓库不包含完整可安装的模组包。

## C++

使用 Visual Studio 2022 x64 C++ 工具、CMake 3.24+、Ninja、vcpkg 的 `directxtk` / `rapidcsv` / `spdlog`（含 fmt）。CommonLibSSE-NG 7.2.0 和所需 MinHook 源码已经包含，无须初始化 git submodule。当前源码已补齐原公开快照遗漏的 cmake/Prebuilt.cmake、CommonLibSSE.cmake、sourcelist.cmake 和 testlist.cmake，并允许它们被 Git 跟踪。辅助模块取自本机已核验的 CommonLib 10.0.1 源码，sourcelist 按本工程 7.2.0 的实际文件生成；引擎头文件及 ABI 仍为 7.2.0。

在 x64 Native Tools 环境，从仓库根目录执行，将示例工具链路径替换为自己的 vcpkg 路径：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --target Ysmir --parallel 6
```

DLL 和配置输出到 `build/Data/SKSE/Plugins/`。必须配合生成该版本 `RuntimeForms.h` 的相同 ESP 使用；单独安装 DLL 不构成完整的 Ysmir 模组。

本次构建使用 MSVC 19.44、fmt 12.1.0、spdlog 1.16.0。不同工具链重编译不保证文件哈希相同。运行时目标为 Steam 1.7.104.0、SKSE64 2.3.1、Address Library v13 / All in One 1.7.104；不捆绑游戏、SKSE 或地址库。

## 数据

先按 [数据构建器说明](../data-builder/README.md) 准备本地输入并生成 ESP，将同一次输出的 `RuntimeForms.h` 复制到 `src/`，新增 ID 时也保留同次输出的 FormIds.json 分配表，再编译 DLL。不能单独压缩或重排最终 ESP 的 FormID。

## Papyrus

使用自己的 Creation Kit PapyrusCompiler、游戏脚本接口和 `TESV_Papyrus_Flags.flg`。include 目录包含 `package/Source/Scripts` 和游戏的 `Data/Source/Scripts`。以下六个文件使用 `-op` 编译，输出到本地 `package/Data/Scripts`：

- Ysmir.psc
- YsmirStormScript.psc
- YsmirSlowTimeScript.psc
- YsmirVoicePushEffectScript.psc
- YsmirWhirlwindDamageScript.psc
- YsmirDragonAspectSummonScript.psc

没有原版全局脚本名覆盖。其余 VMAD 使用游戏本身已提供的脚本。

## 验证记录

根目录 `BUILD-REPORT.json` 与 `docs/verification/` 记录本次集成产物的文件/记录检查，不表示游戏内验证或公开二进制/资产。实际验证边界见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。

`tools/regression/verify_latest_native.py` 是可选的只读 PE/地址核对工具，需要自有 SkyrimSE.exe、对应 DLL、pefile 2024.8.26 / capstone 5.0.6。本次执行了编译和只读文件/二进制核对，没有启动游戏。


## 整合回归

在 x64 Native Tools 环境执行：

```powershell
python tools/regression/test_integration.py build-regression
python tools/regression/test_gameplay.py build-regression
python tools/regression/test_core.py build-regression
python tools/regression/test_storm_pex.py package/Data/Scripts/YsmirStormScript.pex build-regression/papyrus-control-flow.json
```

测试编译当前源码的完整风暴/元素适配器及成长、锥形、桥接、原生施法函数体，使用引擎服务替身；元素计时替换为可控墙钟。共 65 项场景/结果：17 项桥接/碰撞/开关、20 组实际风暴适配器状态、9 组元素触发、11 组成长与发射钩子、8 组编译 PEX 控制流。覆盖独立目标时序、新敌人、失效取消、旧风暴记录与 v5 引用恢复、天气调用、成长及发射参数保持。它不模拟真实 Skyrim 的物理、VM、抗性结算或天气画面。
