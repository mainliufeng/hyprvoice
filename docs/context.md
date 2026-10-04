# 光标前文辅助 · 2026-10-03

原实现只缓存进程内最近提交的语音文字，而且原文听写模式没有使用它。该缓存已删除，避免不同窗口的内容混合或把发送粘贴请求当作编辑器的真实内容。

新的可选能力读取**当前输入框光标前的实际文字**，包括键盘输入和手动修改。每次开始录音都重新读取，不跨窗口保存历史。实际音频采集先启动，读取过程中先缓冲录音，避免等待前文接口丢失开头。界面显示本次参考前文的最后 96 个字符；模型接收的是配置范围内的完整前文。

## 行为与边界

- 使用 [AT-SPI Text](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/method.Text.get_text.html) 和 [caret offset](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/method.Text.get_caret_offset.html)，不通过全选、复制、移动光标或读取剪贴板获取前文。
- 只查询 Hyprland 当前窗口对应的应用进程、其中具有真实焦点且可编辑的文本控件；查询前后再次验证窗口身份。光标之后的文字不包含在前文中；有选区时以前面边界为准。
- 默认前文上限为 1024 个 Unicode 字符，可设置 1..2048。查询在辅助进程内执行，超过 1.5 秒或接口失败即停止。查询失败明确显示未读到前文，不伪造已读取，也不使用其他窗口的历史代替。
- 无障碍接口可能未开放，应用进程和控件的对应关系也可能不兼容。本次实测覆盖 GTK Wayland 编辑器和启用无障碍渲染的 Chrome 网页 textarea／contenteditable。当前 Chrome 启动器会读取 `~/.config/chrome-flags.conf`；已设置 `--force-renderer-accessibility=complete`，但**已有 Chrome 进程需要重新启动一次才加载标志**。本次没有关闭用户的日常 Chrome。Electron 和终端的前文尚未验证。原有普通听写上屏与前文覆盖范围是两回事。
- 密码角色控件不获取其值，不向模型发送本次转录；文本修改指令被拒绝。密码框仍可接受本地原文听写。其他敏感内容无法靠控件角色自动识别，启用外部模型意味着允许发送当前普通输入框前文。
- 原文听写有可用且非空前文时，调用文本模型进行上下文纠错。提示词要求只返回本次新增文字，前文仅用于判断同音词、专名、指代和标点，不重复前文、不擅自展开代词或补写事实。选区修改仍需要明确确认。
- 模型失败时保留识别原文，显示错误并等待确认，提供“使用原文”，不自动插入替代结果。取消、提交或识别为空回到待机时清除前文。

## 开启条件

默认配置为：

```json
"context": {"enabled": false, "max_chars": 1024}
```

开启需设置 `context.enabled=true`，配置真实 `llm.base_url`、`llm.model` 与对应密钥环境变量，然后重启服务。启用后，发送本次转录、当前输入框的一段前文；指令模式还包括选中文字。录音不上传。

**本机已于 2026-10-04 获得明确授权并开启。** 用户确认读取当前输入框光标前文（包括键盘输入），允许现有 DeepSeek 处理该前文与本次转录。实际配置为 `deepseek-chat`、最多 1024 字前文，密钥独立保存于 `~/.config/hyprvoice/llm.env`（权限 600），服务环境已核对加载成功。其他机器仍默认关闭。

## 验证范围

[8 项真实控件检查](context-desktop-result.json)验证实际前文、光标之后不读取、键盘修改、Unicode 长度、窗口隔离、返回原窗口和密码框保护。`tests/context_desktop_test.py` 在独立 Hyprland 与 D-Bus 内操作真实 GTK 控件，拒绝默认日常桌面。

正式程序的公开录音经专用 PipeWire 音频源进入 ASR 后，[7 项录音与前文检查](context-audio-result.json)验证真实前文传入、界面不抢焦点、模型未配置时阻止自动提交、明确使用原文后真实上屏。

本地 HTTP 契约测试确认字段、提示词及失败／取消路径，服务端为明确测试替身。另已完成以下**真实 DeepSeek 请求**：

- [10 条开发用例](context-model-score.json)：有前文 10/10、无前文 7/10。
- [8 条新增用例](context-extra-score.json)：有前文 8/8、无前文 4/8。覆盖 PipeWire、DeepSeek、Mobius、Wayland，以及否定／数字／代词／前文注入。
- [正式桌面链路 10 项](context-model-desktop-result.json)：公开 WAV 经真实 PipeWire、ASR、实际编辑器前文进入模型，预览不抢焦点，明确确认后插入、默认自动追加不重复前文，提交／取消清除前文。[实际界面](context-preview.png)已视觉检查。
- 安装后使用实际用户配置和运行服务的密钥环境，公开 Hyprland 用例请求成功，并正确返回前文中的专名拼写。

评分忽略标点、空格、英文大小写，是小样本文本对照，不是整体语音识别率。无前文开发用例中，小企鹅“五”变为“5”也被严格计为不匹配。新增 8 例已参与提示词调试，**不是未见的盲测集**。初轮有专名未改对及 30 秒请求超时；最终提示词增加与测试名词不同的 Kubernetes 示例，最终 36 次有／无前文请求全部完成。正式桌面前文请求同样成功，日常仍可能遇到接口超时，失败须明确确认原文。未读取、录制或发送日常现场内容进行验收。

已准备 10 条独立编写的公开上下文用例：项目专名、否定、前后数字变更、指代、新话题、空前文、上下文中的注入文字和前文重复。配置获授权的真实模型后，可复现：

```bash
apps/hyprvoice/build/context_probe /path/to/authorized-model-config.json \
  apps/hyprvoice/tests/fixtures/context.jsonl > /tmp/hyprvoice-context-results.jsonl
python3 apps/hyprvoice/tests/context_score.py \
  apps/hyprvoice/tests/fixtures/context.jsonl /tmp/hyprvoice-context-results.jsonl
```

每条分别请求有／无前文的真实模型，计分忽略标点、空格和英文大小写。专名用例是针对性的纠错检查，不代表真实语音整体准确率。原有 10 例与 `tests/fixtures/context-extra.jsonl` 的 8 例均已执行。

## Chrome 前文读取

[Chromium 官方说明](https://www.chromium.org/developers/accessibility/testing/automated-testing/ax-inspect/)提供 `--force-renderer-accessibility` 入口。真实 Chrome 检查见 [浏览器结果](context-browser-result.json)，覆盖启动器读取 flags 配置、光标边界、键盘修改、密码框与网页编辑区。测试使用独立 profile 和 D-Bus。默认无标志的 Chrome 测试读不到前文；开启后各检查通过。复杂网页仍可能受到控件无障碍实现、进程对应、256 节点／深度 32 和查询时限的限制；未读到时界面明确提示，不伪造上下文。

录音启动顺序调整后的 [20 项完整桌面回归](context-full-desktop-result.json) 和 2 项 CTest 通过，涵盖真实 PipeWire、Wayland／XWayland／Kitty、噪声、取消、焦点保护、轻按／长按与重复提交。
