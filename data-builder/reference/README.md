# 本地构建参考输入

0.4.0 构建器要求下列文件，均未随公开仓库上传：

| 文件 | SHA-256 | 来源 |
| --- | --- | --- |
| `ForcefulTongue.esp` | `E3DB5982E36CBB1CBA7D33029B4B2B3E394B80CBCBDB1C722D949E3056E2BAC2` | [Forceful Tongue 3.0.9.1](https://www.nexusmods.com/skyrimspecialedition/mods/36276)，按作者条款取得 |
| `Ysmir-0.3.0-reference.esp` | `DC122F95B4582F9E4F5D94437239AE0F4EC5E3FFBED8D6179A6A58DE672A5CC5` | 此前私人整合构建的冻结产物，含所选第三方记录，不是公开下载依赖 |

两者必须在同一目录。构建器严格校验哈希；不是任意同名 ESP 都能替代。官方 Skyrim.esm、Update.esm、Dawnguard.esm、Dragonborn.esm 及字符串 BSA 从自己的游戏安装读取。

当前不能仅靠本仓库和上游下载文件重建 ESP；需要已经持有这组本地输入。将构建器改为直接读取上游独立模组是后续工作，本次只公开现有实现。

这些文件只供构建，不能加入游戏加载顺序。源码许可不改变输入数据和资产的原有许可。
