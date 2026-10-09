# Cornice 多 seat 语音输入

本功能位于 `codex/cornice-seat-input`，配合 Hyprland 的
`codex/cornice-agent-desktop` 特性分支使用。

F8/F9 继续使用现有 Hyprvoice 服务。支持 `human-input-target-v1` 的合成器返回
人当前实际操作的 seat、应用与焦点令牌；Hyprvoice 的录音目标、剪贴板及粘贴快捷键
均绑定这个目标。进入 Agent 接管后输入 Agent 应用，离开后输入人的应用。
不修改 `activewindow` 的原有含义，也不需要 dotfiles 转发脚本。

只读、锁屏、Launcher/任务面板焦点不回落到隐藏应用。切换工作区、窗口焦点或接管
状态后，旧目标失效；录音中的输入路径失效会结束采集并保留结果，避免 F8 释放被
新模式吞掉后继续录音。普通听写的明确“输入”动作可重新确认原 seat、原窗口和当前
光标，仍检查密码、选区与最终粘贴目标。指令替换继续绑定原编辑控件与选区。

恢复原 seat、原窗口后，保留结果的“输入”按钮随焦点事件刷新可用性；这只更新
按钮状态，不自动提交，也不改录音时的目标令牌。实际点击后才重新确认输入路径。
原生呈现期间，主 seat 的浮层关闭不会恢复隐藏的人类应用焦点。

每个 seat 的剪贴板 owner 独立保存；向一个 seat 输入不会销毁其他 seat 的剪贴板。

隔离验证在 Cornice 的 `voice-seat-verify.py` 中运行真实 GTK 应用、主 seat F8、
长驻生产 `Desktop` 和实际 Wayland 剪贴板，覆盖跨 seat 输入、过期拒绝、明确重试及
Launcher 焦点。`seat_input_probe` 只在测试中以固定文本代表识别结果，不替代生产
ASR 或麦克风。真实录音链及悬浮层另由 `voice-session-verify.py` 验证；其中音频和
识别器是隔离测试替身，不能据此声称实测了麦克风、ASR 准确率或物理按键延迟。

普通 Hyprland 继续使用已有单 seat 行为；已协商新能力后，目标查询失败会拒绝输入，
不会降级到隐藏的人的应用。
