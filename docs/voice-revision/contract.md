# 当前待确认正文的语音修改（可审阅逻辑版）

基线：`25a058f`（共同祖先为5577415；与main a5d44d1分叉，保留 7e82cc0、4cb409a、52f5ec4、25a058f 四个 Cornice 提交）。候选已在特性分支整合main a5d44d1的尾句保留修复。独立 worktree/分支 `codex/current-result-voice-edit`；不安装、不重启、不合 main、不 push。

## 状态与不变量

- 初次听写使用既有 F8 press/release、Audio、ASR、context 和 ProcessText，不新建采集或模型框架。
- `raw`：本轮原始最终识别，修改要求绝不写入 raw。`text`：处理版本。`effective_text`：当前选定版本（preferred_raw 为真时投影 raw，否则 text），是复制、输入、F9 唯一正文来源。
- `session_id`：每次新听写递增；`result_revision`：正文发布、切换、手动保存、语音修改、撤销时递增。UI 未保存草稿不进入正文；未来编辑 UI 在草稿未保存时阻止 F9 或先保存。
- `recognition_warning`：原始ASR不确定性，成功修改不静默清除；`revision_error`为本次修改失败，成功后清除过时失败提示。
- `revision_instruction`：本次修改要求，含录音预览与识别结果，和正文分离。模型仅收到 original_body、revision_instruction 两个用户数据字段。正文中的指令不是系统指令。
- 当前无 ready 待确认正文：F9 返回明确提示，不查询选区/剪贴板/应用正文。
- ready → starting → recording → finalizing → rewriting → ready。全程保留正文；只在模型输出有效且会话/正文版本均匹配时发布新版。不会自动输入。
- 任一录音/识别/网络/无效输出错误回到 ready，正文、原ASR、既有警告保留。取消正在执行的修改进入 cancelling，等待旧 worker 收尾后恢复 ready；收尾前不接纳新工作。迟到结果不能覆盖正文。
- 修改成功后支持再次 F9。`undo-revision` 恢复最近一次成功修改开始前的完整正文及其选定状态和警告，最多保留20轮。手动保存/显式切换来源后旧语音撤销链失效。
- target、seat、窗口、控件及原 guard 不因浮层操作而重新绑定。F9 只用原目标的本地元数据做权限核验，不读取前文/选区作为修改数据；保留现有密码、可靠控件、录音变化与粘贴前校验边界。

## IPC / UI 接口（已与原型线程约定）

| 动作 | 接口 | 行为 |
| --- | --- | --- |
| 开始口述修改 | `command` 或 `revise` | 同一入口；修改录音中再次触发则结束采集；其他 busy 阶段拒绝重入 |
| 结束 | `stop` 或 `command-release` | 后者用于 F9 松开，防止误停 F8，模型完成后仅展示 |
| 取消修改 | `cancel`（修改执行中） | 恢复修改前正文，作废异步版本；不取消整轮 |
| 放弃整轮 | `discard` | 明确清除本轮结果；ready 时原 cancel 行为保留 |
| 重试文本处理 | `retry` | 原修改要求+原正文快照重试；正文变化则拒绝 |
| 撤销修改 | `undo-revision` | 恢复修改前正文，不输入 |
| 手动保存正文 | `edit-result JSON` | JSON 为 `{session_id, base_revision, text}`，过期或空内容拒绝 |
| 复制/输入 | `copy` / `insert-current` | 使用 effective_text；保留原目标、选区、密码、最终发送检查 |

`status.state` 新增 `effective_text`、`session_id`、`result_revision`、`revision_instruction`、`recognition_warning`、`revision_error`、`action_note`、`undo_revision_available`。生产无测试注入接口。command_mode 只表示正在采集/处理修改要求，ready 始终表示正文。

## UI 变化与确认边界

并行分支 `codex/result-review-prototype` 负责 `docs/result-review-prototype/**`，用户确认整体原型后才实施生产 UI。本线程先完成上述逻辑与接口；现有“撤销修改”按钮仍为旧来源切换，语音修改撤销先经undo-revision验证，后续原型接线时明确区分。已有浮层仅作语义正确的文案/正文展示适配，不实施整体布局、编辑器或新按钮。

待原型确认后：“语音修改”按钮与 F9 均调用 revise；ready 显示当前正文和“语音修改”；修改中正文留在正文区，要求单独显示，禁用输入/复制/编辑/撤销，保留“结束录音/取消修改”；完成后显示新版、输入/复制/编辑、“撤销本次修改”。所有按钮使用不抢占目标的既有浮层，不将焦点改成插入目标。取消修改与放弃本轮需明确区分。F8 保持原录音用途。

当前安装的 F8/F9 按下/松开绑定由 compositor 查询证实存在；dotfiles agent-session/hyprland.lua 对应 F8 press/release、F9 command/stop。候选绑定示例改为 command/command-release，避免F9松开误停F8；本轮不更改现场配置，实际安装时需同步绑定。运行二进制为0.2.1，安装与/proc可执行文件SHA均为7def571c580268c2866fdffdf12815ade78762450342945088ea6c3da6353d76；版本号不提供源码提交证明，尚不能证明运行包含全部4个提交，但候选源码保留全部4个。
