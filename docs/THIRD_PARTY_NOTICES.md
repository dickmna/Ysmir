# Ysmir 0.9.1 来源与许可

Ysmir 原创代码继续保留 `LICENSE` 中 dickmna and Codex 的 MIT 声明。组合 DLL 链接 CommonLibSSE-NG 7.2.0，按 GPL-3.0-or-later 提供，附完整对应源码与 `COPYING`。游戏数据和第三方资产仍归原作者，其权限不因私有 FormID、静态链接或本次重构而改变。

| 来源 | 作者 / 版本 | 本版使用 |
| --- | --- | --- |
| [Forceful Tongue](https://www.nexusmods.com/skyrimspecialedition/mods/36276) | Delta，3.0.9.1 | 所选龙吼图、完整 Dragon Aspect、QoL、全部库存模型、召唤模型纹理、私有召唤桥接逻辑 |
| [Elemental Fury Overhaul and Compatibility](https://www.nexusmods.com/skyrimspecialedition/mods/93681) | Unstable，v3 | Dynamic Shaders 及 FT 兼容记录，不含原动画包 |
| [Slow Time Edit](https://www.nexusmods.com/skyrimspecialedition/mods/62686) | Kittytail | wrapper、玩家豁免和实际减速图 |
| [Whirlwind Sprint Redone](https://www.nexusmods.com/skyrimspecialedition/mods/49359) | Aleksi134，Stagger v2 | 冲刺经过目标的效果链 |
| [Shout Progression](https://www.nexusmods.com/skyrimspecialedition/mods/162607) | Elzar125 | 事件/原值缓存、成长及已知词近似思路；未捆绑原源码/DLL |
| [Dynamic Unrelenting Force](https://www.nexusmods.com/skyrimspecialedition/mods/184210) | dickmna | 动态推力适配 |
| [True Cone Shouts](https://www.nexusmods.com/skyrimspecialedition/mods/183927) | dickmna | Soul Tear 锥形数据与 VFX 补偿参考；不再运行时加宽角度或增大初始碰撞半径 |
| [Storm Call Shout Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/179244) | dickmna，Pure SKSE 3.0.1 Beta | 引擎结算、错峰补击与显示边界参考；动态全目标调度为 Ysmir 的额外目标 |
| [Dynamic Animation Casting](https://github.com/ArcEarth/DynamicAnimationCasting/tree/4ec11a6aba1b91f219d2116a751125d62faf588f) | ArcEarth | 所选九条元素狂暴规则的冷却语义参考；不捆绑 DAC DLL |
| Skyrim / Creation Engine | Bethesda Game Studios | 官方 records、模型依赖和 Papyrus 接口 |

DUF、True Cone 和 SCSO 是 dickmna 的同作者项目。公开仓库包含 Ysmir 的实现源码和必要的开源依赖，不附 Bethesda 文件、Nexus 下载包、第三方 ESP 参考输入、模型或纹理；构建这些数据所需的本地输入由使用者按来源条款取得。下面的来源表同时记录私人集成产物的设计和数据来源，并不表示所有列出的资产已在 GitHub 再分发。

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)：commit `7a60f4de794095d7b0f8928d1b930a52e9a7da83`，GPL-3.0-or-later；许可见 `licenses/CommonLibSSE-NG-GPL-3.0.txt` 和外部源码目录。
- [MinHook](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)：1.3.4 / HDE64，许可见 `licenses/MinHook.txt`。
- fmt、spdlog、DirectXTK、DirectXMath、rapidcsv 的原始许可/署名保留在 `licenses/`。
- [Mutagen](https://github.com/Mutagen-Modding/Mutagen)：0.54.4，数据构建器按所附 GPL-3.0-only 提供；不重新许可输入的 Bethesda/Nexus 记录。

FT 与 Slow Time Edit 的独立兼容补丁仅作为结构参考，本包不捆绑其下载文件。所有具体取舍见 `COMPATIBILITY.md`。
