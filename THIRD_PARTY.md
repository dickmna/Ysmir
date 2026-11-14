# 来源与许可

本仓库公开 Ysmir 的生成逻辑、脚本修改与验证工具。游戏文件、参考插件及其模型纹理由构建者自行取得，不放入仓库或 GitHub Release。

| 来源 | 作者 / 本次输入 | 用途与公开范围 |
| --- | --- | --- |
| [Forceful Tongue — Shouts Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/36276) | Delta，3.0.9.1 | 龙吼结构、修正、冥想/黑书/召唤机制及菜单资源的基础。生成器在本地读取 ESP/BSA；仓库包含经改写的必要 Papyrus 逻辑，不附 ESP/BSA、模型或纹理。作者允许署名修改与使用资产，禁止销售；第三方资产仍保留其来源限制。 |
| [Elemental Fury and Battle Fury Shout Overhaul and Compatibility](https://www.nexusmods.com/skyrimspecialedition/mods/93681) | Unstable / Unstable17，3.0，No Shaders 主插件 | 本地读取直接角色攻速与三系效果链；采用 Reduced Shaders 配置。Ysmir 的 HitFrame 事件脚本替代动画配置触发；不发布原插件、DAC/OAR 文件或动画。作者页面不允许直接跨站转载原文件。 |
| [Whirlwind Sprint Redone](https://www.nexusmods.com/skyrimspecialedition/mods/49359) | Aleksi134，1.1，`WhirlwindSprintRedone version 2.esp` | 本地读取敌方碰撞斗篷与踉跄记录。作者页面限制转载、修改与资产使用，故 GitHub 不包含该输入或含有其记录的完整运行包。 |
| [词墙直接解锁的参考模组](https://www.nexusmods.com/skyrimspecialedition/mods/46794) | Nexus mod 46794 | 功能参考；在四个原生词墙脚本的 TeachWord 后加入 UnlockWord。没有捆绑该模组的下载文件。 |
| Skyrim / Dawnguard / Dragonborn | Bethesda Game Studios | 原生记录、脚本、Papyrus 接口和视觉依赖。词墙覆盖及部分短桥接脚本来源于游戏逻辑；不将这些来源重新许可为 Ysmir 原创。 |
| [Mutagen](https://github.com/Mutagen-Modding/Mutagen) | Noggog，0.53.1，GPL-3.0-only | 仅用于构建；通过 NuGet 获取。数据构建器以相同许可证提供，不捆绑其二进制。 |

权限页面核对日期：2026-11-15。页面可能变化，应遵守各来源的实际条款。Ysmir 不代表拥有这些第三方资产的再许可权。

## 许可证范围

- `builder/*.cs` 与项目文件：GPL-3.0-only，见 `builder/LICENSE`。
- 原创 Python/PowerShell 工具、文档、`YSMR_ElementalFury.psc` 和 `YSMR_VoicePush.psc` 的原创部分：MIT，见根目录 `LICENSE`。
- 其余 Papyrus 源码包含 Delta 或 Bethesda 的既有逻辑及 Ysmir 修改，保留上述来源权利；根目录 MIT 不覆盖原作者部分。
- 稳定 FormID、输入哈希和验证数据用于可复现构建，不授予参考输入的再分发许可。

`YSMR_` 及 `ysmir/` 是当前运行文件的专属命名空间。来源名称仍会出现在构建输入、代码注释与本文件中，以保持准确署名和可追溯性。
