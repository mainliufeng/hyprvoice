# Hyprvoice

独立的 Hyprland 语音输入工具。保留现有 Fcitx 拼音，用快捷键录音，通过悬浮条预览，最终粘贴到应用。
构建与运行不依赖 `voice-input`、Fcitx 开发库或 `vinput-registry`。本地模型文件可复用现有下载。旧项目移植的辅助代码已移除，独立实现范围见 [实现说明](docs/independent-implementation.md)。

## 能力与边界

- 同一快捷键轻按切换录音、按住说话；停止、取消和原文提交。
- 固定 X-ASR 中英流式模型＋离线二次精修，热词、Silero VAD 无语音拒绝、长录音保护。
- 本地听写无需网络；可选 OpenAI 兼容接口提供纠错、整理和翻译。
- 选中文字后口述修改指令，预览结果，确认时重新核对选区再替换。
- 默认自动提交普通听写。录音期间切换窗口则保留结果，返回原窗口手动提交。
- 非抢焦点的 GTK4 layer-shell 悬浮条；普通应用 Ctrl+C/V，配置中的终端 Ctrl+Shift+C/V。

实时文字显示在悬浮条，最终一次上屏。没有输入框内预编辑。可选的 [前文辅助](docs/context.md) 通过无障碍接口读取当前输入框的光标前文，覆盖范围取决于应用。
同一窗口内移动光标无法可靠检测；如果录音时需要编辑，关闭 `auto_commit`，完成后明确选择插入位置。
指令模式始终需要确认，不会将识别出的指令原文当成替换文字。LLM 失败时普通听写保留原文，指令模式不修改选区。

剪贴板只处理 UTF-8 文本。默认将结果留在剪贴板；`clipboard_restore` 的延迟恢复是尽力而为，应用粘贴没有完成回执。
选区读取使用复制快捷键，会临时使用剪贴板，不能恢复完整富文本／图片。键盘输入、应用窗口和复制能力必须可用。
终端只用于普通听写；不要在终端里通过选区修改模式替换命令，终端选区通常不代表可替换的编辑区域。

## 构建

本机需要 C++20、CMake、GTK4、gtk4-layer-shell、PipeWire、AT-SPI、libcurl、sherpa-onnx C API、wl-clipboard、hyprctl。
Arch 可用对应软件包 `gtk4 gtk4-layer-shell pipewire at-spi2-core curl sherpa-onnx wl-clipboard cmake`。
JSON 依赖固定为 nlohmann/json 3.11.3 单文件版，随源码提供并保留 MIT 许可。

```bash
cmake -S apps/hyprvoice -B apps/hyprvoice/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/hyprvoice/build -j2
ctest --test-dir apps/hyprvoice/build --output-on-failure
```

## 配置真实模型

只支持以下两套 transducer 文件名，其他模型不能直接替换。模型参考：[sherpa-onnx 模型文档](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/index.html)。
先确认自己的模型目录和 Silero VAD 文件；可保留已下载的模型，也可复制到 `~/.local/share/hyprvoice/` 后使用。

```bash
python3 apps/hyprvoice/scripts/configure.py \
  --streaming ~/.local/share/hyprvoice/models/x-asr-960ms-streaming-zipformer-transducer-zh-en-punct-int8 \
  --offline ~/.local/share/hyprvoice/models/x-asr-zipformer-transducer-zh-en-punct-int8 \
  --vad ~/.local/share/hyprvoice/silero_vad.onnx \
  --hotwords ~/.config/hyprvoice/hotwords.txt
apps/hyprvoice/build/hyprvoice doctor
apps/hyprvoice/build/hyprvoice serve
```

`--hotwords` 可省略。每行一个词，可写 `Hyprland:5.0`；UTF-8，支持 `#` 注释。配置存在时脚本拒绝覆盖。
`hyprvoice init` 仅生成默认配置，不下载或伪造模型；使用前必须核对里面的实际路径。
`HYPRVOICE_CONFIG=/绝对路径/config.json` 可以隔离配置和测试。

文本处理在配置中填写 `llm.base_url` 和 `llm.model`，密钥只从 `llm.api_key_env` 指定的环境变量读取，默认 `HYPRVOICE_API_KEY`。
HTTPS 默认开启验证；可信本地 HTTP 服务需明确设置 `llm.allow_http=true`。场景提示词位于 `prompts`，修改配置后重启。
默认 `scene=raw`、`context.enabled=false`，不调用任何大模型。启用纠错／整理／翻译或指令模式会发送本次转录与选中文字；启用前文辅助还会发送当前输入框光标前最多 1024 个字符（可设置 1..2048）。不再缓存不同窗口的最近提交记录，不发送录音。转录和前文不写入持久日志；本地 CLI `status` 会返回本次转录与实际使用的前文，回到待机时清除前文。文本处理失败保留识别原文并等待用户确认，不自动插入失败后的替代结果。

## 日常运行

本机已按用户要求安装，当前状态和快捷键见 [安装记录](docs/installation.md)。下面是其他机器的通用安装步骤。

```bash
bash apps/hyprvoice/scripts/install.sh
systemctl --user import-environment WAYLAND_DISPLAY HYPRLAND_INSTANCE_SIGNATURE DISPLAY
systemctl --user daemon-reload
systemctl --user enable --now hyprvoice.service
```

可选密钥文件 `~/.config/hyprvoice/llm.env` 只供服务加载，设置权限为 600。安装脚本不创建配置、不启动服务、不修改 Fcitx 或 Hyprland。
Hyprland 重启后应从该会话重新导入环境；也可在 Hyprland 自动启动项中运行环境导入与服务重启。

Lua 配置使用 `config/hyprvoice.lua` 的绑定；仍使用 hyprlang 时参考 `config/hyprvoice.conf`。二者选一，根据自己的配置合并。
[Hyprland 官方绑定文档](https://wiki.hypr.land/Configuring/Basics/Binds/)。

| 按键示例 | 动作 |
|---|---|
| F8 | 轻按开始／结束，按住说话后松开结束 |
| F9 | 选中文字后按住说修改指令，松开处理 |
| Super+Alt+Enter | 提交处理结果 |
| Super+Alt+O | 提交识别原文 |
| Super+Alt+Escape | 取消当前会话 |
| Super+Alt+1 / 2 / 3 / 4 | 原文／纠错／整理／英文翻译 |

CLI 同样支持 `start stop toggle press release command cancel commit raw status scene NAME quit`。
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
