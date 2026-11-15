# 构建 Ysmir 1.2.3

运行成品不需要 SKSE 或其他模组。下面是源码重建所需工具和本地输入。

## 工具与输入

- Windows PowerShell、.NET SDK 9、Python 3。
- Skyrim Special Edition / Anniversary Edition 的官方 Data 目录及 BSA，包括构建时读取的 HearthFires；生成插件的实际 masters 只有 Skyrim、Update、Dawnguard、Dragonborn。
- Forceful Tongue 3.0.9.1 的 `ForcefulTongue.esp`，以及同目录同名 BSA。
- Nexus 93681 v3.0 的 `Main Files/No Shaders/Elemental Fury for MCO ADXP.esp`。
- Nexus 49359 v1.1 的 `WhirlwindSprintRedone version 2.esp`。
- 官方 Creation Kit PapyrusCompiler.exe、原生脚本 import 源码、`TESV_Papyrus_Flags.flg`。
- [Champollion](https://github.com/Orvid/Champollion) 用于对编译出的 PEX 做独立反编译核对。

参考插件的实际 SHA256 记录在 `reports/build.json` 的 `inputs`。NuGet 会获取固定声明版本的 Mutagen 0.53.1。

## 命令

将以下路径替换为自己的路径。`PapyrusImports` 可以有多个目录，需包含原生 Actor、ObjectReference、Quest、词墙监听器及其他被引用脚本的定义。

```powershell
.\rebuild.ps1 `
  -GameData 'D:\Games\Skyrim Special Edition\Data' `
  -ForcefulTongue 'D:\ModSources\ForcefulTongue\ForcefulTongue.esp' `
  -ElementalFury 'D:\ModSources\ElementalFury\Main Files\No Shaders\Elemental Fury for MCO ADXP.esp' `
  -WhirlwindSprint 'D:\ModSources\WhirlwindSprint\WhirlwindSprintRedone version 2.esp' `
  -PapyrusCompiler 'D:\Games\Skyrim Special Edition\Papyrus Compiler\PapyrusCompiler.exe' `
  -PapyrusImports @('D:\Papyrus\Vanilla', 'D:\Papyrus\DLC') `
  -PapyrusFlags 'D:\Papyrus\TESV_Papyrus_Flags.flg' `
  -Champollion 'D:\Tools\Champollion\Champollion.exe'
```

脚本会生成 ESP、SEQ 和 21 个资源，编译全部 11 个 PSC，反编译 PEX，再运行 `verify.py`。构建与验证结果写入 `reports/`。不会安装到游戏目录，也不会启动游戏。

发布记录的 ID 来自 `inputs/stable-formids.json`。新记录只能追加，不能重新压缩编号或重用已保留编号。输入中的原版投射物字段是独立验证基线，用于发现射程、速度和视觉参数的意外变化。

`inputs/miraak-shouts.json` 记录已审计的米拉克专用与灰胡子版本的原生冷却，验证器检查五条必要覆盖和其余原生继承。三英雄的共享玩家龙吼也会逐条检查。

若本地保留了 1.2.0 ESP，可运行 `python audit_1_2_3.py --baseline 'D:\Baselines\Ysmir-1.2.0.esp' --game-data 'D:\Games\Skyrim Special Edition\Data'`，独立检查本次修改范围及米拉克覆盖中的原生词语、法术链接和任务数据。

## 生成本地安装包

在全新源码检出中成功运行 `rebuild.ps1` 后，运行：

```powershell
python package_runtime.py
```

安装 ZIP 写入 `dist/`，只包含 ESP、11 个 PEX、SEQ、清单中的 21 个资源和使用说明，不包含源码或构建缓存。可用 MO2 或 Vortex 安装。

完整运行包包含参考模组派生记录，遵守来源许可，仅在本地生成；GitHub Release 提供源码包。
