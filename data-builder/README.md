# Ysmir 0.5.0 数据构建器

.NET 9 / Mutagen.Bethesda.Skyrim 0.54.4。三个位置参数依次为官方游戏 Data 目录、上游输入目录、输出目录。

```powershell
dotnet restore data-builder/Ysmir.DataBuilder.csproj --configfile data-builder/NuGet.Config
dotnet run --project data-builder/Ysmir.DataBuilder.csproj -c Release -- "D:/Games/Skyrim/Data" "data-builder/reference" "artifacts"
Copy-Item artifacts/RuntimeForms.h src/RuntimeForms.h
Copy-Item artifacts/Ysmir.esp package/Data/Ysmir.esp
```

输入目录放入以下原始文件，文件版本由 `SourceRules.cs` 内的 SHA-256 严格校验：

| 文件 | 来源 |
| --- | --- |
| ForcefulTongue.esp | FT 3.0.9.1 |
| Elemental Fury for MCO ADXP.esp | Nexus 93681 Dynamic Shaders 3 |
| Elemental Fury Shout Overhaul - Forceful Tongue Patch.esp | 93681 对应 FT 补丁 |
| Slow Time Edit.esp | Nexus 62686 |
| WhirlwindSprintRedone version 2.esp | Nexus 49359 version 2 |

这些上游插件是构建输入，无须在运行时启用；未随公开仓库分发。SCSO v3 控制逻辑与投射物参数由源码规则构建，True Cone 由原生适配器处理。

依次加载 Skyrim.esm、Update.esm、Dawnguard.esm、Dragonborn.esm 的获胜记录。官方英文字符串从其 BSA 临时读取；不读取 Plugins.txt，不修改游戏目录。

`SourceRules.cs` 明确列出 26 个根及其机制来源；`SelectedBuild.cs` 克隆所需依赖、恢复官方已有数值并执行小范围特例。完整 FT Dragon Aspect、FT 远古龙裔、150 级 FT 英灵和 SCSO 保持约定。惊惧新增区域 50/75/100 属于 FT 的范围机制，保留原版 7/15/24 级恐惧和 30 秒持续。

`FormIds.json` 是 EditorID 到稳定私有 ID 的分配表，仅用于升级时保留标识。它没有记录载荷，也不读取任何旧 Ysmir ESP。普通原版视觉记录直接引用，库存模型引用汇合到对应私有模型。可达性剪枝去掉未选内容，Battle Fury 不进入输出。

输出 `Ysmir.esp`、`RuntimeForms.h`、`selected-records.json`。当前为 350 条记录，其中 324 条私有记录和 26 条官方 SHOU 表现覆盖；118 个玩家 MGEF、104 个玩家法术，无未解析引用。没有任务、全局变量、词墙或世界修改。

同次生成的 Header 必须与 ESP 配套重编译 DLL；不要单独压缩/重排最终 FormID。输出是普通 ESP，未标记 ESL。第三方模型/纹理需按各自许可另行取得；生成器不把资产下载或捆绑进公开源码。
