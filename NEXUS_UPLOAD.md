# Ysmir Nexus 发布记录

用户要求后续上传使用官方 API。完整操作方法保存在本机 [nexus-api-upload skill](C:/Users/19566/.codex/skills/nexus-api-upload/SKILL.md)，包括签名上传头、版本更新、介绍 PATCH 和回读验证。

官方依据：[API 文档](https://api-docs.nexusmods.com/)、[OpenAPI schema](https://api.nexusmods.com/openapi.yaml)、[官方 upload-action](https://github.com/Nexus-Mods/upload-action)。

## 2026-11-16 验证成功的 1.2.5 发布

| 项目 | 已验证值 |
| --- | --- |
| 账户 | `dickman290` |
| 模组页面 | https://www.nexusmods.com/skyrimspecialedition/mods/199223 |
| 游戏 domain / ID | `skyrimspecialedition` / `1704` |
| 网页 mod ID | `199223` |
| 全局 mod ID | `7318624471607` |
| 持久的主文件更新链 ID | `8391426` |
| 1.2.5 全局 version ID | `7318625111833` |
| 1.2.5 下载页 file ID | `839449` |
| 安装包 | `Ysmir - Shout System Overhaul v1.2.5.zip` |
| 字节数 | `429334` |
| SHA-256 | `ad073f637d002260ff2cdca25cc97a98927213be30b2c880b8b3584fd7dc4a20` |

上传使用 `/v3/uploads` → 签名 URL PUT → `/finalise` → 等待 `available` → `/v3/mod-files/8391426/versions`。旧 1.2.0 主文件归档；当前 1.2.5 为主下载，模组公开，网页扫描结果为 Safe to use。公开页的 VirusTotal 摘要与安装包 SHA-256 相同。

不要把下载页 file ID `839449` 当作更新链 ID，也不要误用另一条已归档的 `8391027` 更新链。下一次上传前仍需回读版本列表，查实际上一版本的全局 ID。

本次介绍修正直接使用 `PATCH /v3/mods/7318624471607`，仅传 `description`，返回 204。回读核对完整 BBCode、27 条龙吼、公开状态、版本号和主文件；版本仍为 1.2.5，安装包未变。

正文源文件是 [NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode)。以用户中文原文为准；本次仅获准修正冰霜吐息的伤害时间、旋风冲刺的踉跄表述、风暴召唤的同伴伤害保护表述。不要自行加入版权说明、测试免责声明或原文未提到的米拉克改动说明。

文件简介源文件是 [NEXUS_FILE_DESCRIPTION.txt](NEXUS_FILE_DESCRIPTION.txt)，内容为 `Full standalone install.`。当前 1.2.5 及归档 1.2.0、1.2.1、1.2.2 的公开文件简介均已核对为此句。发布时从该文件读取简介，不复用历史上传日志中的旧文案。

模组正文与文件简介必须分别检查。当前官方 API 尚未提供修改现有文件版本简介的接口；此次通过文件管理页 More actions → Edit → File options 修改文字，未上传新文件。主文件 ID、版本和归档状态保持原值。
