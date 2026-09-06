# 从公开源码构建 0.4.0

本仓库可以编译原生 DLL，包含它对应的 `src/RuntimeForms.h`。重新生成 ESP 还需要两个未公开的本地输入，见 [数据构建器](../data-builder/README.md)。本仓库不包含完整可安装的模组包。

## C++

使用 Visual Studio 2022 x64 C++ 工具、CMake 3.24+、Ninja、vcpkg 的 `directxtk` / `rapidcsv` / `spdlog`（含 fmt）。CommonLibSSE-NG 7.2.0 和 MinHook 1.3.4 源码已经包含，无须初始化 git submodule。

在 x64 Native Tools 环境，从仓库根目录执行，将示例工具链路径替换为自己的 vcpkg 路径：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --target Ysmir --parallel 6
```

DLL 和配置输出到 `build/Data/SKSE/Plugins/`。必须配合生成该版本 `RuntimeForms.h` 的相同 ESP 使用；单独安装 DLL 不构成完整的 Ysmir 模组。

历史构建使用 MSVC 19.44、fmt 12.1.0、spdlog 1.16.0。不同工具链重编译不保证文件哈希相同。运行时目标为 Steam 1.7.104.0、SKSE64 2.3.1、Address Library v13 / All in One 1.7.104；不捆绑游戏、SKSE 或地址库。

## 数据

先按 [数据构建器说明](../data-builder/README.md) 准备本地输入并生成 ESP，将同一次输出的 `RuntimeForms.h` 复制到 `src/`，再编译 DLL。不能单独压缩或重排最终 ESP 的 FormID。

## Papyrus

使用自己的 Creation Kit PapyrusCompiler、游戏脚本接口和 `TESV_Papyrus_Flags.flg`。include 目录包含 `package/Source/Scripts` 和游戏的 `Data/Source/Scripts`。以下五个文件使用 `-op` 编译，输出到本地 `package/Data/Scripts`：

- Ysmir.psc
- YsmirSlowTimeScript.psc
- YsmirVoicePushEffectScript.psc
- YsmirWhirlwindDamageScript.psc
- YsmirDragonAspectSummonScript.psc

没有原版全局脚本名覆盖。其余 VMAD 使用游戏本身已提供的脚本。

## 验证记录

根目录 `BUILD-REPORT.json` 与 `docs/verification/` 记录的是此前私人集成产物的文件/记录检查，并不证明游戏内功能正确，也不表示报告中的二进制或资产已公开。具体已确认遗漏见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。

`tools/regression/verify_latest_native.py` 是可选的只读 PE/地址核对工具，需要自有 SkyrimSE.exe、对应 DLL、pefile 2024.8.26 / capstone 5.0.6。本次公开没有执行该工具、重新构建或启动游戏。
