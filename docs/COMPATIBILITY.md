# 兼容补丁与来源取舍

## FT 与 Slow Time Edit

已查到并读取 [Forceful Tongue – Slow Time Edit Patch](https://www.nexusmods.com/skyrimspecialedition/mods/90759?tab=files)，文件 385206，v1.0（2023-05-06）。该 ESP 有三个 SPEL 覆盖，连接 62686 的 wrapper 和 FT 的显示辅助效果，不包含 FLST 修改。

参考其施放转接关系；不照抄补丁的 10/12/30 秒，按用户规则恢复 8/12/16 秒。FT 显示辅助效果不是额外攻速能力，无须引入。私有 wrapper 使用 YsmirSlowTimeScript，再由主线程 native 入口施放实际减速法术；原版数值和玩家豁免分开处理。

## FT 与元素狂暴

已读取 [93681 v3 Dynamic Shaders](https://www.nexusmods.com/skyrimspecialedition/mods/93681?tab=files) 及其 FT 兼容补丁。使用补丁配套效果、关联法术与着色数据；原版攻速、15 秒持续和冷却保留。三个元素分别 50% 概率、2 秒触发冷却，沿用来源设置。

原包还带可选 Battle Fury DAC 逻辑，但本次按用户明确指示排除。运行包没有 Battle Fury 改动，也不需要原包的 DAC/OAR 动画目录。玩家 native hitframe 事件承担所选元素狂暴触发。

## True Cone / 动态推力 / Shout Progression

[True Cone Shouts](https://www.nexusmods.com/skyrimspecialedition/mods/183927?tab=posts) 作者说明纯 SKSE 主文件不需要 Shout Progression 的 ESP 补丁；可选 Soul Tear ESP 涉及胜出顺序。本包把所选 Soul Tear 图直接生成成私有数据，True Cone 统一使用 15° 与本次施放快照。

[Dynamic Unrelenting Force 源码](https://github.com/dickmna/DynamicUnrelentingForce) 的 Shout Progression 附加模块提供推力倍率接入参考。本版使用公共幅度快照，仅在脚本推力入口乘一次；不再同时增幅 projectile impactForce。

[Shout Progression 162607](https://www.nexusmods.com/skyrimspecialedition/mods/162607) 本地源码仅四个文件，没有完整工程或稳定外部 API。因此公共核心依据其事件/快照模型独立实现，没有把原 DLL 当可调用底层，也没有要求并装原 DLL。

## SCSO / FT

使用用户的 [SCSO v3](https://www.nexusmods.com/skyrimspecialedition/mods/179244) 源码及其数值。没有查到可供核对的专用 FT 补丁；直接让 SCSO 单独拥有玩家风暴控制流程，并移除私有风暴根上的旧 Papyrus 控制器。只用一套扫描、错峰雷击和天气管理。

[FT 3.0.9.1](https://www.nexusmods.com/skyrimspecialedition/mods/36276?tab=description) 的 Dragon Aspect 按完整关联图导入，包括经 perk 间接到达的战斗能力、召唤法术及远古龙裔 NPC；不仅复制三个表面法术。英灵召唤另按用户明确例外保留 FT 属性并固定 150 级；龙魂觉醒的远古龙裔保留 FT 原值，不套用英灵的 150 级。

## 库存模型

[185443 Mesh Replacer](https://www.nexusmods.com/skyrimspecialedition/mods/185443) 的三个 loose NIF（SelfWhite、PinkMagic、Frost）原样保留。它们不是 DLL/API，三个公共替换文件不能独自表达 FT 的全部独特模型，所以所需 FT 私有 STAT/模型路径仍保留。最终包只带引用到的 FT 模型、召唤特效及所需纹理，不带 Phantom Form 或词墙模型。

这里只覆盖明确选定的组件。Stormcrown、Edmond's、Legendary Shouts、Thunderchild、Ultimate Shouts 等额外大修的补丁没有整合。
