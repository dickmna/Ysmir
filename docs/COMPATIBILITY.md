# 兼容补丁与来源取舍

## FT 与 Slow Time Edit

已查到并读取 [Forceful Tongue – Slow Time Edit Patch](https://www.nexusmods.com/skyrimspecialedition/mods/90759?tab=files)，文件 385206，v1.0（2023-05-06）。该 ESP 有三个 SPEL 覆盖，连接 62686 的 wrapper 和 FT 的显示辅助效果，不包含 FLST 修改。

参考其施放转接关系；不照抄补丁的 10/12/30 秒，按用户规则恢复 8/12/16 秒。FT 显示辅助效果不是额外攻速能力，无须引入。私有 wrapper 使用 YsmirSlowTimeScript，再由主线程 native 入口施放实际减速法术；原版数值和玩家豁免分开处理。

## FT 与元素狂暴

已读取 [93681 v3 Dynamic Shaders](https://www.nexusmods.com/skyrimspecialedition/mods/93681?tab=files) 及其 FT 兼容补丁。使用补丁配套效果、关联法术与着色数据；原版攻速、15 秒持续和冷却保留。按三元素×三词阶的九条 DAC 规则分别 50% 概率、2 秒冷却；重吼不重置，多个词阶并存时逐条检查。计时使用包含菜单时间的墙钟，触发本身要求游戏未暂停。依据 [DAC 固定源码](https://github.com/ArcEarth/DynamicAnimationCasting/blob/4ec11a6aba1b91f219d2116a751125d62faf588f/src/Framework.cpp)。

原包还带可选 Battle Fury DAC 逻辑，但本次按用户明确指示排除。运行包没有 Battle Fury 改动，也不需要原包的 DAC/OAR 动画目录。玩家 native hitframe 事件承担所选元素狂暴触发。

## True Cone / 动态推力 / Shout Progression

[True Cone Shouts](https://www.nexusmods.com/skyrimspecialedition/mods/183927?tab=posts) 作者说明纯 SKSE 主文件不需要 Shout Progression 的 ESP 补丁；可选 Soul Tear ESP 涉及胜出顺序。本包把所选 Soul Tear 图直接生成成私有数据，将旧 Ysmir 固化的 15° 改回 [上游构建器的 10°](https://github.com/dickmna/TrueConeShouts/blob/5b4d661ae597806b6e035c0ee6986be1a9e0759e/tools/SoulTearConeBuilder/Program.cs)。不导入该可选包的 99999 复活门槛或额外受控数量。0.7.0 不把其他锥形角度抬高，不猜测半角/全角，不按成长倍率修改运行时切线。0.9.0 进一步按用户要求取消锥形起始碰撞半径成长；只读引擎半径、切线与已移动距离计算视觉补偿，smoothstep 从原始尺寸过渡，固定既有锚点。保留用户对 TCSF 实测有效的评价，作为游戏测试参照。0.9.1 所有模型统一为原锚点整体缩放，保留原生动画和既有浓度补偿；删除 0.9.0 的分层和 UV 处理。0.9.2 再将视觉倍率改为按成长后实例射程的传播进度，从 1 平滑增至龙魂距离倍率；删除视觉计算中的半径/角度依赖，旧 fVisualTransitionDistance 键忽略。没有固定四倍，不按模型名或节点名分支。不导入 TCSF 的角度加宽、初始半径下限、固定抬高或多层叠乘倍率。

[Dynamic Unrelenting Force 源码](https://github.com/dickmna/DynamicUnrelentingForce) 的 Shout Progression 附加模块提供推力倍率接入参考。本版使用公共幅度快照，仅在脚本推力入口乘一次；不再同时增幅 projectile impactForce。

[Shout Progression 162607](https://www.nexusmods.com/skyrimspecialedition/mods/162607) 本地源码仅四个文件，没有完整工程或稳定外部 API。因此公共核心依据其事件/原值缓存和成长目标独立实现；实例快照是为保留 Ysmir 0.5.0 的额外行为，没有把原 DLL 当可调用底层，也没有要求并装原 DLL。

## SCSO / FT

对照选定的 [SCSO Pure SKSE 3.0.1 Beta 源码](https://github.com/dickmna/Storm-Call-Shout-Overhaul/blob/89739fd8b91c6b3f230ff96b8b231ee0eae2738d/beta/src/skse/main.cpp)：该分支保留引擎主雷击，复制原目标与法术补击，并修复显示边界；它不含动态寻敌。Ysmir 的动态寻敌属于既定附加目标，不能称作 SCSO 原样移植。0.8.0 按新的用户要求采用每目标独立随机节奏，SKSE 发现和复查目标，短脚本走原版 RemoteCast 单次引雷 1/2/3 次；新目标可以独立加入。原版空中/瞄准标记机制复用，既有发射钩子只做取消检查与成长跟踪，删除方向/alwaysHit/autoAim 改写。0.7.0 纠正手工扣血及 30/60/120 默认值，采用官方 40/60/80 实际法术与 ResistShock；天气改回原版 magicSetWeatherScript。没有并行运行另一套原版引雷循环。

[FT 3.0.9.1](https://www.nexusmods.com/skyrimspecialedition/mods/36276?tab=description) 的 Dragon Aspect 按完整关联图导入，包括经 perk 间接到达的战斗能力、召唤法术及远古龙裔 NPC；不仅复制三个表面法术。英灵召唤另按用户明确例外保留 FT 属性并固定 150 级；龙魂觉醒的远古龙裔保留 FT 原值，不套用英灵的 150 级。

## 库存模型

按用户最终要求，0.7.0 不打包 Nexus 185443 的 Community Shaders 专用模型替换（SelfWhite、PinkMagic、Frost），不以社区着色器为依赖。FT 的全部 17 个龙吼库存模型原样打包，另带 Call of Valor 召唤特效和 4 张所需纹理。FT 的 Battle Fury / Phantom Form 菜单模型文件也保留在资源包内，但未添加对应玩法记录或控制器；词墙模型不打包。

这里只覆盖明确选定的组件。Stormcrown、Edmond's、Legendary Shouts、Thunderchild、Ultimate Shouts 等额外大修的补丁没有整合。
