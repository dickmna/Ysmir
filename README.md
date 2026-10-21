# Ysmir

Skyrim 玩家龙吼成长与选定机制整合。**0.9.0**，目标运行时为 **Steam 1.7.104.0 / SKSE64 2.3.1 / Address Library v13**。

沿用 Shout Progression 的龙魂成长目标，整合 FT、True Cone Soul Tear、动态推力、元素狂暴修复和 Slow Time 玩家豁免。运行包为一个 DLL、一个 ESP、一个 INI、六个 Papyrus 脚本，以及所需 FT 模型和纹理。

0.9.0 取消锥形初始碰撞半径成长，保留原始角度和龙魂距离成长；VFX 从原始大小按传播距离平滑放大，保持锚点，不加高度偏移、不换 NIF。0.8.0 的风暴机制继续保留：将风暴改为每个目标独立随机错峰：SKSE 动态识别敌人，短脚本按词阶通过原版 RemoteCast 施放 1/2/3 次。新敌人加入，失效目标停止；删除 C++ 定向发射参数改写，保留成长、天气及既有视觉修复。元素狂暴恢复九条独立规则及重吼不清冷却，Soul Tear 基础锥角回到上游的 10°。不再运行时加宽角度。

按 0.5.0 清单逐项保留其余功能：26 条玩家龙吼、78 个词位、完整 FT Dragon Aspect、150 级 FT 英灵、成长与旧实例倍率、动态推力、视觉尺寸/浓度、风暴边界修复和菜单模型。保留 True Cone 总开关、视觉开关和 `GetDragonSoulCount` 接口；按新要求删除锥形碰撞成长及其开关。Battle Fury 继续不改。

默认未花费龙魂数为 S，幅度、距离、范围、持续时间、非锥形碰撞/视觉倍率为 `1+0.05S`，冷却为 `0.95^S`，不设龙魂上限。锥形视觉按原始半径、切线与距离计算补偿，并用 smoothstep 过渡；基础参考默认 4 倍、过渡距离 256 单位，可在 INI 调整。Slow Time 保持固定 8/12/16 秒。NPC 使用官方龙吼记录。

已完成本地编译与离线验证，尚未做游戏内测试。完整安装包和匹配源码包在本地交付，没有部署到游戏。重构前源码已备份到 GitHub 的 v0.8.0 标签（5c53daf）；本次源码使用 codex/ysmir-distance-visuals 分支。**源码 ZIP 不能作为运行模组安装。**

源码包含 C++、Papyrus、配置、.NET 数据构建器、CommonLibSSE-NG/MinHook 和许可证。原始上游 ESP、模型、纹理和 Bethesda 文件不在源码包内；构建输入由使用者自行取得。`FormIds.json` 固定 ID，`RuntimeForms.h` 必须与同次生成的 ESP 配套。

[完整行为](docs/CURRENT_BEHAVIOR.md) · [改动](docs/CHANGELOG.md) · [结构](docs/ARCHITECTURE.md) · [机制来源](docs/COMPATIBILITY.md) · [验证边界](docs/KNOWN_ISSUES.md) · [构建](docs/BUILD.md)

Ysmir 原创代码采用 [MIT](LICENSE)；链接 CommonLibSSE-NG 的组合 DLL 按 [GPL-3.0-or-later](COPYING) 提供。数据构建器采用 [GPL-3.0-only](data-builder/LICENSE-GPL-3.0.txt)。第三方数据和资产保留原作者权利。见 [来源与许可](docs/THIRD_PARTY_NOTICES.md)。
