# Hyprvoice 实际验收 · 2026-10-03

实现目标是独立的 Hyprland 语音工具：保留 Fcitx 拼音，录音、识别、悬浮预览、粘贴，以及可选文本处理。未修改旧 `apps/voice-input` 或日常桌面的配置与服务。

## 识别回归

正式 `hyprvoice replay` 重放旧工具的 [127 条清单](../../voice-input/docs/quality/2026-10-02/manifest.jsonl)，使用同一套已下载的 X-ASR 流式／离线模型、同一热词文件和 Silero VAD。包括中英文、20/10 dB 白噪声、10 dB 列车声、42–73 秒长句，以及静音／纯噪声。

- 127 条全部成功；最终文本 **127/127** 与旧工具 [优化后的结果](../../voice-input/docs/quality/2026-10-02/all-refined-final.jsonl) 逐条相同。
- 3 条无语音样本没有输出文字。
- 中文 holdout CER：干净 9.23%、20 dB 白噪声 9.49%、10 dB 白噪声 10.26%、列车声 9.23%。这些是该小型公开集上的结果。
- 英文 holdout 的列车声错误仍较高：CER 24.27%。精简架构没有消除模型已有的弱点。
- [本次逐条输出](asr-results.jsonl)、[评分](asr-score.json)保留在本目录；不是微信／豆包的识别结果。

```bash
HYPRVOICE_CONFIG=/path/to/config.json apps/hyprvoice/build/hyprvoice replay \
  apps/voice-input/docs/quality/2026-10-02/manifest.jsonl > /tmp/hyprvoice-results.jsonl
python3 apps/voice-input/tests/quality/quality.py score \
  apps/voice-input/docs/quality/2026-10-02/manifest.jsonl \
  /tmp/hyprvoice-results.jsonl /tmp/hyprvoice-score.json
```

## 真实桌面与文本处理

使用真正的 Hyprland 0.56.2，GTK4 4.22.5、gtk4-layer-shell 1.3.0、PipeWire 1.6.9、sherpa-onnx 1.13.8。测试全部发生在独立运行目录、独立 D-Bus 会话和 1280×720 的虚拟显示器中；没有操作日常窗口或读取日常剪贴板。

公开 FLEURS 录音经 `paplay → 本次测试专用音频源 → PipeWire → 正式 ASR` 处理；不是直接把字符串塞进界面。测试键盘使用真实 Wayland virtual-keyboard 协议，编辑器是实际 GTK TextView，终端是实际 Kitty。读取编辑器文本缓冲区和终端接收字节核对上屏。测试自身创建的音频模块在结束时卸载。

独立 Hyprland 嵌套在临时 Sway headless 后端中。系统 Aquamarine 请求高于父合成器公布的接口版本，故仅在 `/tmp` 编译私有 Aquamarine v0.15.1，调整三个 registry bind 为双方版本的较小值。系统库、Hyprland 输入／剪贴板实现和本应用均没有因此替换或模拟。这个验收覆盖真实 Hyprland 的应用链路，不能代替物理桌面验收。

[真实 DeepSeek 桌面验收](llm-desktop-result.json)记录了 Wayland、XWayland、Kitty 粘贴、预览不抢焦点、纯噪声不插字、取消、错误窗口拒绝、终端拒绝选区修改。用户明确授权后，向现有 DeepSeek 接口发送公开转录、选中文字和测试修改指令；没有上传录音。选区修改确认前编辑器不变，确认后替换成功。

[该次修改结果](llm-command-result.json)：公开原文为关于伊拉克局势的报道，测试口述指令为 “Make this text shorter.”。合成语音被 ASR 识别成 “Make this tank shorter.”；LLM 仍给出较短中文改写，并保留“无法保证阻止”的否定。这里只证明一条真实修改链路及这个样例的效果，不能推断所有指令可靠或 LLM 总能修复识别错误。

[真实拼音共存复测](pinyin-result.json)通过：通过 Ctrl+Space 切换拼音，再用实际键盘输入 `nihao` 和空格，编辑器收到“你好”。Fcitx 与 Hyprvoice 同时运行；测试仅禁用旧 Vinput 插件和通知，保留 Fcitx 的正常输入功能。

最终桌面复测的 20 项检查全部通过，另见 [本地验收结果](desktop-result.json)：包括轻按／长按、默认自动提交、拒绝重复提交、切走再回来仍需确认、正常退出清理 socket，以及未配置 LLM 时保留选区和拒绝把修改指令当替换文字。测试脚本位于 `tests/desktop_check.py`，拒绝在默认 `/run/user/<uid>` 日常桌面运行。额外需要独立 Hyprland、Fcitx 拼音、paplay/pactl、Kitty、grim、espeak-ng 和 ffmpeg；`--llm-env` 只应指向已获授权使用的凭据文件，默认不调用 LLM。

若复现拼音共存，可加 `--fcitx --coexist-only`；完整本地功能验收不带这两个参数。

## 当前工作区直接试运行

已在忽略的 `build/local-assets/` 复制 VAD 和热词，并生成不启用 LLM 的 `build/local-config.json`。模型权重复用本机已下载的文件，不依赖旧应用进程或旧仓库代码。它们是本机运行材料，不随 Git 提交；新机器仍按 README 配置真实模型。

```bash
HYPRVOICE_CONFIG="$PWD/apps/hyprvoice/build/local-config.json" \
  apps/hyprvoice/build/hyprvoice serve
```

本机没有自动运行这条命令、安装用户服务或接管旧快捷键。

## 构建与自动检查

CMake 构建通过；生产构建可以关闭 `BUILD_TESTING`，不依赖测试键盘或 Fcitx 开发库。CTest 覆盖长音频不丢失／重复采样、中英文拼接、子进程字面文本与超时，以及 HTTP 请求结构、真实客户端收到成功／失败／空结果／取消时的行为。HTTP 服务是测试内的 loopback 测试替身，不用于正式功能；真实外部调用按上一节验收。

## 尚未覆盖

- 没有真人对物理麦克风讲话；本机默认麦克风保持原来的静音状态。未接管当前语音快捷键或启用新服务。
- 用户无法使用微信／豆包输入法，因此没有同录音竞品结果，也不声称质量已对标或胜过它们。
- Clipboard paste 没有应用完成回执；同窗口移动光标无法可靠检测。富文本剪贴板恢复、所有终端、浏览器密码框、所有应用复制行为不在本次覆盖范围。
- 一条真实 LLM 修改验收不等于语义保真、纠错／翻译／整理的完整质量评估。
