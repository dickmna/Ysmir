# Ysmir

Open-source Skyrim shout progression and integration framework. **0.5.0**, targeting **Steam Skyrim 1.7.104.0 / SKSE64 2.3.1 / Address Library v13**.

以施放事件和龙魂倍率快照为公共核心，接入 True Cone、动态冷酷之力、SCSO 和选定龙吼机制。运行时为一个 DLL、一个生成的 ESP、一个 INI 和五个 Papyrus 桥接脚本。

0.5.0 统一投射物几何与缓存，补齐惊惧范围，改用原始上游模组生成数据，移除了旧版 Ysmir ESP 构建依赖。源码、数据构建器与脚本已编译并核对文件/记录；游戏内效果由用户验证。

## 行为

普通龙吼使用选定模组机制及官方已有参数的基础值；新增机制没有官方对应参数时沿用来源参数。Dragon Aspect 和远古龙裔完整采用 FT 3.0.9.1；Storm Call 采用 SCSO v3；Call of Valor 英灵采用 FT 属性与行为并固定 150 级。Battle Fury 完全保持原版。

默认 `S` 为未消费龙魂，幅度、距离、范围、持续时间、碰撞与视觉基础倍率均为 `1 + 0.05S`，冷却为 `0.95^S`。Slow Time 持续时间固定。视觉围绕原始模型中心缩放，补偿会随旋转与父节点变换计算。26 条玩家龙吼、78 个词位使用私有映射和 MagicShout 分类。

## 构建与公开范围

仓库提供原生代码、Papyrus 源码、配置、.NET 9 数据构建器、匹配的 CommonLibSSE-NG/MinHook 源码及许可。**源码包不能直接作为完整模组安装。** 第三方 ESP、模型、纹理与游戏文件需自行取得，运行包未上传。

构建器直接读取官方 masters、Forceful Tongue、Elemental Fury Dynamic Shaders 及其 FT 补丁、Slow Time Edit、Whirlwind Sprint Redone。版本与哈希见 [数据构建器](data-builder/README.md)。`FormIds.json` 仅保存稳定的记录标识，不包含旧版 ESP 的记录内容。Shout Progression 提供公共成长的设计参考，不依赖它的 DLL。

[完整行为](docs/CURRENT_BEHAVIOR.md) · [0.5.0 改动](docs/CHANGELOG.md) · [几何与快照](docs/ARCHITECTURE.md) · [兼容补丁依据](docs/COMPATIBILITY.md) · [验证范围](docs/KNOWN_ISSUES.md) · [构建](docs/BUILD.md)

## 许可

Ysmir 原创代码采用 [MIT](LICENSE)；链接 CommonLibSSE-NG 的组合 DLL 按 [GPL-3.0-or-later](COPYING) 提供。数据构建器采用 [GPL-3.0-only](data-builder/LICENSE-GPL-3.0.txt)。第三方代码、游戏/模组记录和资产保留各自权利。详见 [来源与许可](docs/THIRD_PARTY_NOTICES.md)。
