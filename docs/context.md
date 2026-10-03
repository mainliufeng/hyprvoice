# 光标前文辅助 · 2026-10-03

原实现只缓存进程内最近提交的语音文字，而且原文听写模式没有使用它。该缓存已删除，避免不同窗口的内容混合或把发送粘贴请求当作编辑器的真实内容。

新的可选能力读取**当前输入框光标前的实际文字**，包括键盘输入和手动修改。每次开始录音都重新读取，不跨窗口保存历史。界面显示本次参考前文的最后 96 个字符；模型接收的是配置范围内的完整前文。

## 行为与边界

- 使用 [AT-SPI Text](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/method.Text.get_text.html) 和 [caret offset](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/method.Text.get_caret_offset.html)，不通过全选、复制、移动光标或读取剪贴板获取前文。
- 只查询 Hyprland 当前窗口对应的应用进程、其中具有真实焦点且可编辑的文本控件；查询前后再次验证窗口身份。光标之后的文字不包含在前文中；有选区时以前面边界为准。
- 默认前文上限为 1024 个 Unicode 字符，可设置 1..2048。查询在辅助进程内执行，超过 1.5 秒或接口失败即停止。查询失败明确显示未读到前文，不伪造已读取，也不使用其他窗口的历史代替。
- 无障碍接口可能未开放，应用进程和控件的对应关系也可能不兼容。本次实测覆盖 GTK Wayland 编辑器，未证明 Chrome、Electron、浏览器网页或终端的前文可读。原有普通听写上屏与前文覆盖范围是两回事。
- 密码角色控件不获取其值，不向模型发送本次转录；文本修改指令被拒绝。密码框仍可接受本地原文听写。其他敏感内容无法靠控件角色自动识别，启用外部模型意味着允许发送当前普通输入框前文。
- 原文听写有可用且非空前文时，调用文本模型进行上下文纠错。提示词要求只返回本次新增文字，前文仅用于判断同音词、专名、指代和标点，不重复前文、不擅自展开代词或补写事实。选区修改仍需要明确确认。
- 模型失败时保留识别原文，显示错误并等待确认，提供“使用原文”，不自动插入替代结果。取消、提交或识别为空回到待机时清除前文。

## 开启条件

默认配置为：

```json
"context": {"enabled": false, "max_chars": 1024}
```

开启需设置 `context.enabled=true`，配置真实 `llm.base_url`、`llm.model` 与对应密钥环境变量，然后重启服务。启用后，发送本次转录、当前输入框的一段前文；指令模式还包括选中文字。录音不上传。

**本机目前保持关闭，LLM 仍未配置。** 用户此前授权只覆盖一次公开文字测试，本次尚未授权将日常输入框前文发送给 DeepSeek。没有可调用的本地文本模型，也没有自动用外部模型替代。

## 验证范围

[8 项真实控件检查](context-desktop-result.json)验证实际前文、光标之后不读取、键盘修改、Unicode 长度、窗口隔离、返回原窗口和密码框保护。`tests/context_desktop_test.py` 在独立 Hyprland 与 D-Bus 内操作真实 GTK 控件，拒绝默认日常桌面。

正式程序的公开录音经专用 PipeWire 音频源进入 ASR 后，[7 项录音与前文检查](context-audio-result.json)验证真实前文传入、界面不抢焦点、模型未配置时阻止自动提交、明确使用原文后真实上屏。

本地 HTTP 契约测试确认前文与转录分别传递、上下文仅为数据和禁止重复前文的提示词，以及失败／取消路径；服务端为明确测试替身。这些检查**不是模型效果验收**。本次未向外部模型发送请求，尚未验证同音词纠正效果或与无上下文模型的对照。授权模型使用后需完成真实文本对照，再开启日常推断。

已准备 10 条独立编写的公开上下文用例：项目专名、否定、前后数字变更、指代、新话题、空前文、上下文中的注入文字和前文重复。获授权配置真实模型后，执行：

```bash
apps/hyprvoice/build/context_probe /path/to/authorized-model-config.json \
  apps/hyprvoice/tests/fixtures/context.jsonl > /tmp/hyprvoice-context-results.jsonl
python3 apps/hyprvoice/tests/context_score.py \
  apps/hyprvoice/tests/fixtures/context.jsonl /tmp/hyprvoice-context-results.jsonl
```

每条分别请求有／无前文的真实模型，计分忽略标点、空格和英文大小写。专名用例是针对性的纠错检查，不代表真实语音整体准确率。该对照本次尚未执行。
