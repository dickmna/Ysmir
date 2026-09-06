# Ysmir

Open-source Skyrim SKSE shout progression and integration framework. Current source snapshot: **0.4.0**, targeting **Steam Skyrim 1.7.104.0 / SKSE64 2.3.1 / Address Library v13**.

以施放事件和龙魂倍率快照为公共核心，整合 True Cone Shouts、Dynamic Unrelenting Force、Storm Call Shout Overhaul，以及选定的龙吼机制。一个原生 DLL、一个生成的 ESP、一个 INI 和五个轻量 Papyrus 桥接脚本构成运行时。

**这是当前实现的源码发布。游戏内效果尚未验证；已确认的惊惧范围遗漏和视觉/碰撞对齐问题见 [已知问题](docs/KNOWN_ISSUES.md)。** 后续讨论的统一 `1 + 0.05S` 视觉倍率和共同中心缩放算法尚未实施，本次没有修改玩法代码。

## 公开内容

- `src/`：公共成长核心、施放分派、True Cone、非锥形视觉、元素狂暴和风暴控制器；包含与当前 ESP 匹配的生成表单映射。
- `package/Source/Scripts/`：五个 Papyrus 桥接脚本的源码。
- `package/Data/SKSE/Plugins/Ysmir.ini`：当前配置。
- `data-builder/`：.NET 9 / Mutagen 数据构建器源码。
- `external/`、`licenses/`：匹配版本的 CommonLibSSE-NG、MinHook 源码及第三方许可。
- `docs/`：功能、构建、来源与兼容补丁依据。

**本仓库及其源码发行包不能直接作为完整模组安装。** 第三方 ESP 参考输入、模型、纹理、Bethesda 游戏文件及私人集成运行包没有上传。原生 DLL 可从本仓库编译；重新生成 ESP 仍需要 [两个本地参考输入](data-builder/reference/README.md)，其中一个是既有私人构建的冻结产物，当前构建器还不能只用上游下载文件替代它。详见 [构建说明](docs/BUILD.md)。

## 当前行为

普通龙吼采用选定模组机制，并恢复官方已有参数的基础值，再应用龙魂成长。新增机制没有对应官方参数时，保留来源参数。Dragon Aspect 和远古龙裔完整沿用 Forceful Tongue 3.0.9.1；Storm Call 使用 SCSO v3；Call of Valor 英灵沿用 FT 属性与行为，等级固定 150。Battle Fury 完全保持原版，不进入私有成长图。

默认 `S` 是未消费龙魂数。幅度、距离、范围、持续时间和碰撞初始半径的倍率为 `1 + 0.05S`，冷却为 `0.95^S`；Slow Time 固定持续时间并单独处理减速。当前视觉基础倍率仍为 `1 + 0.03S`，True Cone 仍保留原来的距离修正、内部节点变换和经验抬高。

26 条玩家龙吼、78 个词位使用生成的私有映射与 `MagicShout` 分类。公共核心按施放保存倍率，始终从原始值计算；NPC、龙和米拉克不获得玩家私有增强。Shout Progression 提供设计参考，没有捆绑或依赖其原 DLL。

[完整行为说明](docs/CURRENT_BEHAVIOR.md) · [兼容补丁依据](docs/COMPATIBILITY.md) · [已知问题](docs/KNOWN_ISSUES.md) · [构建](docs/BUILD.md) · [许可与来源](docs/THIRD_PARTY_NOTICES.md)

## 许可

Ysmir 原创代码保留 [MIT](LICENSE)；链接 CommonLibSSE-NG 的组合 DLL 按 [GPL-3.0-or-later](COPYING) 提供。数据构建器按其 [GPL-3.0-only](data-builder/LICENSE-GPL-3.0.txt) 提供。第三方源码各自保留原许可证。游戏/模组输入、资产和数据记录不因本仓库开源而获得新的许可。
