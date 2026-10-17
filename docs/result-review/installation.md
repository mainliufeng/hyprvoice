# 0.3.0 本机交付与回退

2026-10-18 00:49（Asia/Shanghai），经最终代码、真实GTK截图与验收证据审查放行后执行。功能和完整验收归档基线为 `5878a56a565999ee7762651e3e9cd31e456a18e5`，已从远端 main 的 `a5d44d1` 快进并推送；远端回读一致。后续交付记录提交只修改文档，不改变已验收生产源码 `3730ecb`。

四项 Cornice 提交 `7e82cc0`、`4cb409a`、`52f5ec4`、`25a058f` 及 main 尾句修复 `a5d44d1` 均逐项验证为 main 祖先。没有用缺少多桌面提交的旧 main 覆盖安装。

## 安装核验

- 安装 `~/.local/bin/hyprvoice` 为0.3.0，使用已验收Release产物，没有现场重建或替换模型。安装与运行 `/proc/3215587/exe` 的SHA256均为 `37709bdf4b6cad1a3cd047c1e272f24be676ebfda1949dbc22b026abbfe82612`。
- 切换前确认旧服务 active、idle、非busy且无待确认或可重试结果；仅停止并启动 `hyprvoice.service`。启动后PID3215587，active、idle、非busy、x-asr、model_ready=true。PID为当时观测值，后续重启可变化。
- `config.json`与systemd服务文件哈希前后一致；音源、模型、后端、云端配置和上下文范围未改变。未重启桌面、vinput或PipeWire。
- F9松开从 `hyprvoice stop` 改成 `hyprvoice command-release`，同步dotfiles原始 `linux/desktop/hyprland/hyprvoice.conf` 及生成 `linux/desktop/hyprland/agent-session/hyprland.lua`；配置提交 `9563ed0`。运行时只动态重绑F9，保留一条按下与一条松开；F8绑定结构逐项不变。空闲时调用command-release返回ok且保持idle。
- 无关 `linux/apps/chrome/chrome-flags.conf` 两行本地修改保持原样，未进入本轮提交。main下未跟踪的 `.worktrees/` 保留为本轮审查工作目录。

安装不增加新的识别质量结论。真实麦克风和外部文字模型语义质量未验收；公开音频本机X-ASR回放及私有GTK/音频/多seat结果见 [验收记录](verification.md)。

## 精确回退

私有备份目录：

`~/.local/state/hyprvoice/rollback/20261018-004625-result-review/`

目录和 `rollback.py` 均0700，保存实际旧程序、原始conf、生成Lua和0600 manifest；没有复制凭据文件。旧程序SHA256为 `7def571c580268c2866fdffdf12815ade78762450342945088ea6c3da6353d76`，版本0.2.1。旧运行文件的确切源码提交未能证明，回退使用的是实际备份字节。

只读检查：

```sh
~/.local/state/hyprvoice/rollback/20261018-004625-result-review/rollback.py --check
```

需要回退时，先处理待确认结果，执行：

```sh
~/.local/state/hyprvoice/rollback/20261018-004625-result-review/rollback.py --execute
```

脚本拒绝打断活动会话，核验备份哈希，恢复旧程序及两处F9绑定，只动态重绑F9并重启HyprVoice，核对实际运行哈希。若绑定文件有并发修改，仅在预期替换可核对时恢复对应行，否则停止供检查；不会覆盖其他配置、模型、服务文件或无关dirty。回退不修改已推送的Git历史，本地两绑定恢复会留下预期差异。

本轮已完成脚本语法、备份哈希、权限及安装后的 `--check` 核验，返回rollback_ready=true；未实际切回旧服务，不能称作日常回退演练。
