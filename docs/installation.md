# 本机安装 · 2026-10-03

用户明确要求安装后，Hyprvoice 已安装并启用，随后又更新了 [悬浮界面](ui-update.md)。这个记录对应本次主机，不意味着克隆源码就已完成其他机器的安装。

## 已安装

- 程序：`~/.local/bin/hyprvoice`，校验和与通过验收的 Mobius 构建一致。
- 用户服务：`~/.config/systemd/user/hyprvoice.service`，已启用并处于 `active/running`。
- 配置：`~/.config/hyprvoice/config.json`，权限 600；默认原文听写、自动提交；黑白灰界面已更新。2026-10-04 经明确授权开启前文辅助（最多 1024 字）并配置 DeepSeek。
- 热词：`~/.config/hyprvoice/hotwords.txt`，权限 600。
- 流式／离线模型：`~/.local/share/hyprvoice/models/`；Silero：`~/.local/share/hyprvoice/silero_vad.onnx`。模型是实际复制文件，运行不依赖 Mobius 的构建目录或旧项目代码。
- Hyprland 的 dotfiles 主配置加载 `~/dotfiles/linux/desktop/hyprland/hyprvoice.conf`。命令使用 `~/.local/bin/hyprvoice`，不依赖合成器的 PATH。
- 登录时执行 `~/.config/hypr/scripts/hyprvoice-session.sh`，先导入当前 Wayland/display/Hyprland socket 标识，再重启服务。当前会话也已完成环境导入。新登录尚未实际发生；该启动脚本通过 shell 语法检查，当前启动链路已实际执行。

原有 Fcitx 拼音配置保持原样。旧 `vinput-daemon.service` 仍启用并运行，可继续使用旧入口；本次没有禁用或卸载它。新工具使用 F8，与旧语音入口分开。

## 使用

在目标输入框中按住 **F8** 说话、松开结束；也可以轻按一次开始，再轻按一次结束。默认识别后自动插入。录音时切换窗口会保留结果，回到原窗口后用 **Super+Alt+Enter** 确认；**Super+Alt+Escape** 取消。

默认麦克风原先静音，本次按安装步骤解除静音，音量保持 0.50。服务当前待机，只有触发录音时才创建音频采集流。安装验收没有录制用户现场声音、读取日常剪贴板或向外部模型发送请求。

当前已配置 `deepseek-chat`。F8 在有非空、可读取的前文时会参考前文纠错；输入框为空或不支持读取时，原文模式仍本地听写。F9 口述修改及纠错／整理／翻译已具备真实文本接口配置，指令模式始终需要确认。模型错误保留原文并等待确认。

当前 Chrome 进程尚未带无障碍渲染标志，需要用户方便时重新启动一次 Chrome，之后新启动器会读取已配置的 `~/.config/chrome-flags.conf`。本次没有关闭浏览器或切换日常焦点。GTK 编辑器无需这一浏览器步骤。

## 安装核对

- 构建与 2 项 CTest 通过；安装的二进制与该构建 SHA-256 相同。
- 已安装配置的 doctor 成功，所有真实模型文件与桌面工具可用。
- 服务的真实 IPC 返回 `phase=idle`、空错误，systemd 报告启用且运行。
- 当前 Hyprland 实际加载的 11 条绑定与配置逐条相同，`configerrors` 为空。
- 实际 Hyprland exec 调度器成功运行安装路径中的 `status` 并连接该服务。
- 开发阶段的语音链路与真实虚拟桌面验证见 [验收记录](verification.md)。真人对当前物理麦克风讲话尚需实际使用观察，不能用待机状态或 doctor 代替语音效果验收。

## 停用与恢复

如果需要停用新工具，先在 Hyprland 主配置中注释新增的 `source` 和 `exec-once` 两行，运行 `hyprctl reload`，再执行 `systemctl --user disable --now hyprvoice.service`。仅 disable 服务不会阻止现存登录脚本显式启动它。

安装前的 Hyprland 配置备份在 `~/.config/hyprvoice/install-backup/hyprland.conf.before-install`；只应还原本次新增行，避免覆盖后续其他修改。安装状态备份也记录了原先麦克风静音及旧服务运行状态。需要恢复麦克风静音时执行 `wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 1`。

2026-10-04 更新的 SHA-256 为 `006f2e78f7da0afa928a4dbbc3a68bd49f37da5111ffbd6dc9d4aaeab1cec316`，与验收构建、安装路径及正在运行的 `/proc/<MainPID>/exe` 三方相同；更新前等待服务持续待机，重启后实际 IPC 为 `idle`、空错误。上下文真实读取检查已通过，真实 DeepSeek 文本对照与正式桌面链路均已通过，见 [前文说明](context.md)。

LLM 密钥独立复制到 `~/.config/hyprvoice/llm.env`，文件权限 600，运行时不依赖旧语音项目。启用前配置备份位于 `~/.config/hyprvoice/install-backup/context-before-2026-10-04/`。如需仅停用前文发送，设置 `context.enabled=false` 后重启服务；如果也要停止其他文本处理请求，保持 `scene=raw` 并不使用指令／纠错／整理／翻译。
