# Ysmir 0.4.0 数据构建器

.NET 9 / Mutagen.Bethesda.Skyrim 0.54.4。入口为 `SelectedBuild.cs`，只接受三个位置参数：官方游戏 Data 目录、固定 0.3.0 参考 ESP、输出目录。

```powershell
dotnet restore data-builder/Ysmir.DataBuilder.csproj --configfile data-builder/NuGet.Config
dotnet run --project data-builder/Ysmir.DataBuilder.csproj -c Release -- "D:/Games/Skyrim/Data" "data-builder/reference/Ysmir-0.3.0-reference.esp" "artifacts"
Copy-Item artifacts/RuntimeForms.h src/RuntimeForms.h
Copy-Item artifacts/Ysmir.esp package/Data/Ysmir.esp
```

构建器依序读 Skyrim.esm、Update.esm、Dawnguard.esm、Dragonborn.esm，以官方获胜记录为普通龙吼基线。官方英文字符串从对应 BSA 读取到临时目录；不会读取或写入用户的 Plugins.txt，不会修改游戏目录。

**两个构建时参考文件没有随公开源码提供。** 需在本地放入 `reference/`；SHA-256 在程序中固定校验：

- `Ysmir-0.3.0-reference.esp`：`DC122F95B4582F9E4F5D94437239AE0F4EC5E3FFBED8D6179A6A58DE672A5CC5`。只导入明确选定的行为及模型资产。
- `ForcefulTongue.esp`（3.0.9.1）：`E3DB5982E36CBB1CBA7D33029B4B2B3E394B80CBCBDB1C722D949E3056E2BAC2`。必须与上项参考文件同目录，提供完整 Dragon Aspect 图及火龙灵行为修复。

以上都是构建输入，不是运行依赖，也不是整个旧版原样继承。普通数值从官方 masters 恢复；英灵 NPC 是用户明确例外，沿用 FT 属性并固定 150 级。战斗狂怒完全排除。未被所选根引用的私有记录最终剪除。

输出 `Ysmir.esp`、`RuntimeForms.h`、`selected-records.json`。Header 必须与同次生成的 ESP 一起使用并重新编译 DLL，不要对最终 ESP 单独压缩/重排 FormID。产物为普通 ESP，未标记 ESL。

当前构建：351 条记录、325 条私有记录、26 条仅含表现差异的官方 SHOU。没有未解析引用，没有新任务、全局变量、词墙或世界修改。龙魂数值只在私有图中运行时应用。

构建器遵循所附 GPL-3.0-only 许可；参考游戏/模组记录保留各自权利归属。

公开版本可审阅和编译构建器源码，但完整 ESP 生成仍依赖上述两个本地输入。`Ysmir-0.3.0-reference.esp` 是此前私人整合构建的冻结产物，不能仅下载 Forceful Tongue 或其他上游模组来替代。此版没有重构这项输入依赖。详见 [参考输入](reference/README.md)。
