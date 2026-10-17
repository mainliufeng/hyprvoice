# Hyprvoice

候选版本 **0.3.0**（2026-10-18）；更新内容及验证范围见 [版本说明](docs/release-notes.md)。

main 新增英文长录音尾句恢复与逐样本退化检查，尚未独立发行；实际收益与限制见 [验证记录](docs/late-tail-verification.md)。
项目网站：<https://hyprvoice.liufeng.me>。运行 `hyprvoice --version` 查看程序版本。

独立的 Hyprland 语音输入工具。保留现有 Fcitx 拼音，用快捷键录音，通过悬浮条预览，最终粘贴到应用。
构建与运行不依赖 `voice-input`、Fcitx 开发库或 `vinput-registry`。本地模型文件可复用现有下载。旧项目移植的辅助代码已移除，独立实现范围见 [实现说明](docs/independent-implementation.md)。
本仓库原先位于 Mobius 单仓库的 `apps/hyprvoice/`，拆分时把该路径的提交历史一并迁移过来；GPL-3.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。

## 能力与边界

- 同一快捷键轻按切换录音、按住说话；停止、取消和原文提交。
- 固定 X-ASR 中英流式模型＋离线二次精修，热词、Silero VAD 无语音拒绝、长录音保护。
- 本地听写无需网络；可选 OpenAI 兼容接口提供纠错、整理和翻译。
- F9 口述修改当前待确认正文；支持整段手动编辑、草稿保留与语音修改撤销。
- 录音异常后收尾已有有效音频，失败时保留最后预览；恢复结果标记可能不完整，等待确认。
- 实时与精修识别只有不同片段显示可点标记，可就地选择实际来源候选；文字处理差异单独查看。
- 默认自动提交普通听写。录音期间切换窗口则保留结果，返回原窗口手动提交。
- 非抢焦点的 GTK4 layer-shell 悬浮条；普通应用 Ctrl+C/V，配置中的终端 Ctrl+Shift+C/V。

实时文字显示在悬浮条，最终一次上屏。没有输入框内预编辑。可选的 [前文辅助](docs/context.md) 通过无障碍接口读取当前输入框的光标前文，覆盖范围取决于应用。
可核验的编辑器会检查同窗口控件、光标、选区和内容变化；无法提供编辑器信息的应用只核验原窗口，不能检测该窗口内光标或控件变化。如果录音时需要编辑，关闭 `auto_commit`，完成后明确选择插入位置。
语音修改始终需要确认，不会将修改要求当成正文输入。LLM 失败时普通听写保留识别文字，语音修改保留上一版正文。

剪贴板只处理 UTF-8 文本。默认将结果留在剪贴板；`clipboard_restore` 的延迟恢复是尽力而为，应用粘贴没有完成回执。
F9 仅修改本次结果，不读取应用选区作为修改对象。键盘输入、应用窗口和复制能力必须可用。
终端仍受原窗口与位置核验边界限制；输入命令前请检查正文。

## 构建

本机需要 C++20、CMake、GTK4、gtk4-layer-shell、PipeWire、AT-SPI、libcurl、sherpa-onnx C API、wl-clipboard、hyprctl。
Arch 可用对应软件包 `gtk4 gtk4-layer-shell pipewire at-spi2-core curl sherpa-onnx wl-clipboard cmake`。
JSON 依赖固定为 nlohmann/json 3.11.3 单文件版，随源码提供并保留 MIT 许可。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

## 配置真实模型

只支持以下两套 transducer 文件名，其他模型不能直接替换。模型参考：[sherpa-onnx 模型文档](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/index.html)。
先确认自己的模型目录和 Silero VAD 文件；可保留已下载的模型，也可复制到 `~/.local/share/hyprvoice/` 后使用。

```bash
python3 scripts/configure.py \
  --streaming ~/.local/share/hyprvoice/models/x-asr-960ms-streaming-zipformer-transducer-zh-en-punct-int8 \
  --offline ~/.local/share/hyprvoice/models/x-asr-zipformer-transducer-zh-en-punct-int8 \
  --vad ~/.local/share/hyprvoice/silero_vad.onnx \
  --hotwords ~/.config/hyprvoice/hotwords.txt
build/hyprvoice doctor
build/hyprvoice serve
```

`--hotwords` 可省略。每行一个词，可写 `Hyprland:5.0`；UTF-8，支持 `#` 注释。配置存在时脚本拒绝覆盖。
`hyprvoice init` 仅生成默认配置，不下载或伪造模型；使用前必须核对里面的实际路径。
`HYPRVOICE_CONFIG=/绝对路径/config.json` 可以隔离配置和测试。

### 文本处理（可选）

在配置里填 `llm` 段。`model` 留空就等于没有配置文本处理，需要时会提示“文本处理模型未配置”，不会静默跳过。

| 字段 | 说明 |
|---|---|
| `base_url` | OpenAI 兼容根地址；程序自己拼 `/chat/completions`，不要把这段路径写进去 |
| `model` | 模型名；留空即未配置 |
| `api_key_env` | 从哪个环境变量读密钥，默认 `HYPRVOICE_API_KEY`；密钥不要写进 `config.json` |
| `timeout_seconds` | 1..60，默认 15；接口慢的服务商可以调大 |
| `allow_http` | 默认 false，只接受 HTTPS；只有可信的本地服务才设 true |

密钥放在 `~/.config/hyprvoice/llm.env`（权限 600），由 systemd 的 `EnvironmentFile` 注入服务环境，不写在 `config.json` 里；改完配置或密钥后重启服务。

常用地址：DeepSeek `https://api.deepseek.com/v1`、OpenAI `https://api.openai.com/v1`、本地 Ollama `http://127.0.0.1:11434/v1`（同时设 `allow_http: true`）。换服务商只改 `base_url` 和 `model`。

场景提示词位于 `prompts`，**键名就是场景名**；自己加一个键（例如 `polish`）后 `hyprvoice scene polish` 就能用。`scene` 是运行时状态，`hyprvoice scene` 不写回配置，重启服务回到配置里的值（`backend` 相反，切换成功后会保存）。

默认 `scene=raw`、`context.enabled=false`，两者同时满足才完全不调用大模型。启用纠错／整理／翻译或指令模式会发送本次转录与选中文字；启用前文辅助还会发送当前输入框光标前最多 1024 个字符（可设置 1..2048）。不再缓存不同窗口的最近提交记录，不发送录音。转录和前文不写入持久日志；本地 CLI `status` 会返回本次转录与实际使用的前文，回到待机时清除前文。文本处理失败保留识别原文并等待用户确认，不自动插入失败后的替代结果。

## 日常运行

本机已按用户要求安装，当前状态和快捷键见 [安装记录](docs/installation.md)。下面是其他机器的通用安装步骤。

```bash
bash scripts/install.sh
systemctl --user import-environment WAYLAND_DISPLAY HYPRLAND_INSTANCE_SIGNATURE DISPLAY
systemctl --user daemon-reload
systemctl --user enable --now hyprvoice.service
```

可选密钥文件 `~/.config/hyprvoice/llm.env` 只供服务加载，设置权限为 600。安装脚本不创建配置、不启动服务、不修改 Fcitx 或 Hyprland。
Hyprland 重启后应从该会话重新导入环境；也可在 Hyprland 自动启动项中运行环境导入与服务重启。

Lua 配置使用 `config/hyprvoice.lua` 的绑定；仍使用 hyprlang 时参考 `config/hyprvoice.conf`。二者选一，根据自己的配置合并。
[Hyprland 官方绑定文档](https://wiki.hypr.land/Configuring/Basics/Binds/)。
快捷键不写在 hyprvoice 自己的配置里：每条绑定只是调用一条下面的 CLI 命令，所以可以换成任意按键；“轻按切换＋按住说话”就是同一个键的 `press` 加一条 `bindr release`。两个示例文件等价，改完执行 `hyprctl reload`，不需要重启服务。
悬浮窗使用“录音快捷键／指令快捷键”的通用动作提示，不自动读取外部绑定或假定实际按键是 F8/F9。自定义场景在悬浮窗中直接显示 `prompts` 的键名，内置场景仍显示中文标签；场景名须非空，提示词须为字符串。

| 按键示例 | 动作 |
|---|---|
| F8 | 轻按开始／结束，按住说话后松开结束 |
| F9 | 对当前待确认正文口述修改要求，松开处理（无需应用选区） |
| Super+Alt+Enter | 提交处理结果 |
| Super+Alt+O | 提交识别原文 |
| Super+Alt+Escape | 取消当前会话 |
| Super+Alt+1 / 2 / 3 / 4 | 原文／纠错／整理／英文翻译 |

文字处理失败后可以点击“再试一次”或运行 `hyprvoice retry`，无需重新录音。普通听写重试复用原转录、场景及已读取前文，失败保留识别文字；当前正文的语音修改重试复用原正文和修改要求，失败保留当前正文，绝不把修改要求输入到应用。重试结果始终等待用户确认。修改录音中取消只退出修改，保留正文；明确放弃整轮使用 `discard`。详见 [语音修改契约](docs/voice-revision/contract.md)。

语音结果界面操作见 [结果界面](docs/result-input-flow.md)，Chromium 网页输入支持见 [浏览器输入](docs/browser-input.md)。同窗口控件、光标、选区和内容保护，以及 CLI 位置确认协议见 [目标保护说明](docs/target-protection.md)。默认纠错／整理的保真边界与合成用例见 [策略验证](docs/fidelity-verification.md)。

CLI 同样支持 `start stop toggle press release command command-release revise undo-revision edit-result discard cancel retry commit raw review confirm TOKEN copy status scene NAME backend fun|x-asr quit`。空闲时用 `hyprvoice settings` 打开轻量设置窗口，用 `hyprvoice diagnose` 做不录音、不联网的本地诊断；详见 [设置与诊断](docs/settings-and-diagnostics.md)。更新前保留当前程序和配置，切换及回退步骤见 [迭代切换方案](docs/iteration-installation-rollback-plan.md)。
运行 `doctor` 后还应核对麦克风静音状态；程序不自动取消你的麦克风静音，也不修改默认音频设备。

替换旧语音工具时停用旧 Vinput 插件和旧语音后台，保留 Fcitx 拼音。验证新工具在你的日常应用中可用后再切换，避免两套快捷键同时录音。

## 模块

`src/app.*` 为会话与悬浮界面；`audio.*` 获取 PipeWire 音频；`asr.*` 连接模型，`speech.*` 检测人声并规划长录音任务，`transcript_text.*` 拼接识别文字；
`rewrite.*` 为文本处理；`desktop.*` 为 Hyprland 窗口与剪贴板上屏；`config.*` 和 `process.*` 为配置与进程调用。
常驻程序通过同用户 Unix socket 接收控制命令。没有自定义输入法协议、插件加载框架、模型市场、数据库或跨桌面适配。

## 验证

```bash
hyprvoice transcribe /绝对路径/mono-16khz.wav
hyprvoice replay /绝对路径/manifest.jsonl > results.jsonl
```

重放清单每行包含 `id` 和 `audio`（或 `wav`）；相对音频路径相对于清单所在目录。读取失败输出 `error` 并最终返回非零。
`transcribe` 和 `replay` 调用正式识别代码，不录音、不上屏、不调用 LLM。现有语音测试清单可以直接重放。
本应用自带独立评分器与公开测试清单，录音准备及许可见 [测试集说明](tests/fixtures/README.md)。
具体实际结果和未验证范围见 [验收记录](docs/verification.md)。

本机与 Qwen3-ASR 0.6B/1.7B、Fun-ASR-Nano 的真实对比，以及 ASR＋DeepSeek 工作流的分开评分见 [2026-10-04 笔记本测评](docs/benchmark-20261004.md)。采样、前文条件与复现方式见 [benchmark 协议](docs/benchmark-protocol.md)。

新版悬浮窗的文字、布局、长文本滚动和真实按钮验收见 [界面精修记录](docs/ui-refresh.md)。

连续录音、弱音首尾被精修裁掉，以及语音检测误判后清空预览的问题与实测见 [丢段修复记录](docs/continuous-recording-fix.md)。若显示“语音检测未确认讲话”，已有长句会保留供确认，自动插入暂停；可点击“插入文字”或取消。

## 可选 Fun-ASR-Nano 后端

`./scripts/install.sh --with-fun` 额外编译 CPU worker 并安装经过 SHA256 验证的模型。普通安装不下载 Fun；首次 Fun 安装约需 1.3 GB 模型空间。默认仍为 X-ASR。

服务运行时执行 `hyprvoice backend fun`，等 `hyprvoice status` 显示 `phase: idle`、`backend: fun` 后即可使用原来的 F8/F9。切回执行 `hyprvoice backend x-asr`。模型加载成功后保存选择，下次启动仍使用所选后端；失败保留之前的后端。录音或存在待确认结果时拒绝切换。`hyprvoice doctor` 只检查当前所选后端需要的资产。可调项：`fun.threads`（1..64，解码线程）和 `fun.timeout_seconds`（5..600，默认 120，不含首次加载）。

Fun 使用独立 FSMN VAD，结束录音后识别完整语音段；录音时显示时长和音量，**不提供逐字预览**。最终预览、输入框前文、DeepSeek、取消和上屏继续走同一链路。后端失败时显示错误并保留输入框内容，不自动换模型或插入文字。具体安装、参数和真实验收见 [Fun 后端说明](docs/fun-backend.md)。

## Cornice Agent 桌面

Cornice 多 seat 特性分支的 F8 输入路由、只读边界及隔离验证见
[多 seat 语音输入](docs/cornice-seats.md)。

当前结果语音修改的状态/UI接口与待确认边界见 [语音修改契约](docs/voice-revision/contract.md)。
识别保留、来源候选、编辑草稿与本轮验收边界见 [结果检查](docs/result-review.md)。
