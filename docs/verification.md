# Hyprvoice 实际验收 · 2026-10-03

实现目标是独立的 Hyprland 语音工具：保留 Fcitx 拼音，录音、识别、悬浮预览、粘贴，以及可选文本处理。研发验收阶段未修改旧 `apps/voice-input` 或日常桌面的配置与服务。用户随后授权安装，当前本机状态见 [安装记录](installation.md)。

## 独立实现识别回归

正式 `hyprvoice replay` 使用本应用自己的 [127 条清单](../tests/fixtures/manifest.jsonl)和独立评分器，模型、VAD、热词与公开 WAV 均复制到本应用资产目录。没有包含、链接或运行旧语音项目源码。录音 SHA-256 全部核对通过。

- 127 条成功；3 条静音／纯噪声输出为空。
- 114/127 最终文字与本应用改写前版本逐条相同。按同一新评分器重算，两版整体 CER 为 9.55% → 9.31%（718 → 700 个编辑／7519 个参考字符）。重复加噪和拼接样本并非独立统计试验。
- 中文 holdout CER：干净 9.23%、20 dB 白噪声 9.49%、10 dB 白噪声 10.26%、列车声 9.23%，与改写前相同。
- 英文长录音 dev 为 12.36% → 15.89%，holdout 为 11.24% → 2.70%；中文长录音两条 CER 不变。dev 中文干净样本为 4.90% → 5.59%，英文 holdout 列车声为 24.27% → 24.72%。改写没有保证每条更好，英语噪音仍有明显弱点。
- [独立实现逐条输出](independent-asr-results.jsonl)、[相同评分器的双版本对比](independent-asr-score.json)保留在本目录。

此固定集用于回归诊断；诊断时查看了所有 fold，因此 holdout 不代表本次未见测试集，也不代表微信／豆包结果或全部自由口述效果。

```bash
cd apps/hyprvoice
HYPRVOICE_CONFIG=build/local-config.json build/hyprvoice replay \
  tests/fixtures/manifest.jsonl > build/results.jsonl
python3 tests/score.py tests/fixtures/manifest.jsonl build/results.jsonl \
  --verify-audio --baseline docs/asr-results.jsonl --output build/score.json
```

`asr-results.jsonl` 和 `asr-score.json` 是改写前历史基线。旧版当时与旧插件的 127 条结果相同；这项历史结论不适用于本次独立实现。新旧结果均由新的独立评分器重算后比较。

## 真实桌面与文本处理

随后根据用户反馈更新了正式悬浮界面，新的实际截图与桌面复测见 [界面更新](ui-update.md)。

**本次独立实现复测：** [20 项真实桌面检查](independent-desktop-result.json)全部通过，包括 PipeWire 录音、Wayland／XWayland／Kitty 上屏、噪声拒绝、轻按／长按、自动提交、切换窗口保护及未配置 LLM 时的选区保护。[实际预览截图](independent-preview.png)已视觉检查，显示中文转录和提交控制，编辑器焦点保持不变。本次不发送外部 LLM 请求。[本次真实拼音共存](independent-pinyin-result.json)也通过：旧语音插件禁用时，用实际键盘输入 `nihao` 和空格，编辑器收到“你好”。


使用真正的 Hyprland 0.56.2，GTK4 4.22.5、gtk4-layer-shell 1.3.0、PipeWire 1.6.9、sherpa-onnx 1.13.8。测试全部发生在独立运行目录、独立 D-Bus 会话和 1280×720 的虚拟显示器中（运行目录使用 `/tmp/hvir` 短路径，避免 Unix socket 长度上限）；没有操作日常窗口或读取日常剪贴板。

公开 FLEURS 录音经 `paplay → 本次测试专用音频源 → PipeWire → 正式 ASR` 处理；不是直接把字符串塞进界面。测试键盘使用真实 Wayland virtual-keyboard 协议，编辑器是实际 GTK TextView，终端是实际 Kitty。读取编辑器文本缓冲区和终端接收字节核对上屏。测试自身创建的音频模块在结束时卸载。

独立 Hyprland 嵌套在临时 Sway headless 后端中。系统 Aquamarine 请求高于父合成器公布的接口版本，故仅在 `/tmp` 编译私有 Aquamarine v0.15.1，调整三个 registry bind 为双方版本的较小值。系统库、Hyprland 输入／剪贴板实现和本应用均没有因此替换或模拟。这个验收覆盖真实 Hyprland 的应用链路，不能代替物理桌面验收。

**历史验证，独立 ASR 改写前完成，本次没有重新向外部接口发请求。** [真实 DeepSeek 桌面验收](llm-desktop-result.json)记录了 Wayland、XWayland、Kitty 粘贴、预览不抢焦点、纯噪声不插字、取消、错误窗口拒绝、终端拒绝选区修改。用户明确授权后，向现有 DeepSeek 接口发送公开转录、选中文字和测试修改指令；没有上传录音。选区修改确认前编辑器不变，确认后替换成功。

[该次修改结果](llm-command-result.json)：公开原文为关于伊拉克局势的报道，测试口述指令为 “Make this text shorter.”。合成语音被 ASR 识别成 “Make this tank shorter.”；LLM 仍给出较短中文改写，并保留“无法保证阻止”的否定。这里只证明一条真实修改链路及这个样例的效果，不能推断所有指令可靠或 LLM 总能修复识别错误。

[真实拼音共存复测](pinyin-result.json)通过：通过 Ctrl+Space 切换拼音，再用实际键盘输入 `nihao` 和空格，编辑器收到“你好”。Fcitx 与 Hyprvoice 同时运行；测试仅禁用旧 Vinput 插件和通知，保留 Fcitx 的正常输入功能。

改写前的桌面复测中，20 项检查全部通过，另见 [本地验收结果](desktop-result.json)：包括轻按／长按、默认自动提交、拒绝重复提交、切走再回来仍需确认、正常退出清理 socket，以及未配置 LLM 时保留选区和拒绝把修改指令当替换文字。测试脚本位于 `tests/desktop_check.py`，拒绝在默认 `/run/user/<uid>` 日常桌面运行。额外需要独立 Hyprland、Fcitx 拼音、paplay/pactl、Kitty、grim、espeak-ng 和 ffmpeg；`--llm-env` 只应指向已获授权使用的凭据文件，默认不调用 LLM。

若复现拼音共存，可加 `--fcitx --coexist-only`；完整本地功能验收不带这两个参数。

## 当前工作区直接试运行

已在忽略的 `build/local-assets/` 复制 VAD 和热词，并生成不启用 LLM 的 `build/local-config.json`。模型权重复用本机已下载的文件，不依赖旧应用进程或旧仓库代码。它们是本机运行材料，不随 Git 提交；新机器仍按 README 配置真实模型。

```bash
HYPRVOICE_CONFIG="$PWD/apps/hyprvoice/build/local-config.json" \
  apps/hyprvoice/build/hyprvoice serve
```

以上是研发阶段的工作区启动方式。后续已按用户要求安装用户服务和 F8 快捷键，见 [安装记录](installation.md)。

## 构建与自动检查

CMake 构建通过；生产构建可以关闭 `BUILD_TESTING`，不依赖测试键盘或 Fcitx 开发库。CTest 覆盖长音频不丢失／重复采样、中英文拼接、子进程字面文本与超时，以及 HTTP 请求结构、真实客户端收到成功／失败／空结果／取消时的行为。HTTP 服务是测试内的 loopback 测试替身，不用于正式功能；真实外部调用按上一节验收。

## 尚未覆盖

- 没有真人对物理麦克风讲话。后续安装时已解除默认麦克风静音、启用新服务和 F8 快捷键；这不等于已完成真人口述质量验收。
- 用户无法使用微信／豆包输入法，因此没有同录音竞品结果，也不声称质量已对标或胜过它们。
- Clipboard paste 没有应用完成回执；同窗口移动光标无法可靠检测。富文本剪贴板恢复、所有终端、浏览器密码框、所有应用复制行为不在本次覆盖范围。
- 一条真实 LLM 修改验收不等于语义保真、纠错／翻译／整理的完整质量评估。
