# 版本说明

## 0.3.0 · 2026-10-18

本轮加入当前结果 F9 口述修改、整段编辑草稿、实际音频识别来源候选与异常结果恢复，沿用简洁浮层。正常正文只显示一份；音频差异默认轻标记，点开局部上下候选。文字模型、F9 与手动版本的比较明确作为文字修改，不能冒充音频识别分歧。

设备、连接、积压或时长上限触发停止后，尽量识别有效队列；收尾失败保留最后预览。恢复正文提示可能不完整，等待确认，不自动输入。用户主动取消与异常分开。编辑草稿不会被后台结果或候选替换；完成编辑后再确认输入。复制和输入始终只有正文。

保留四项 Cornice 多桌面提交及 main 英文尾句修复。secondary 编辑在原 seat 的实际 Wayland display 窗口中进行；浮层检查不改变输入目的地。F9 松开绑定使用 `command-release`，避免误停 F8。模型、音源、云端配置和上下文范围沿用现有设置。

验收与边界见 [结果检查](result-review.md)、[语音修改契约](voice-revision/contract.md)。隔离桌面测试不使用日常麦克风；合成 HTTP 契约不代表真实模型质量。安装与最终构建 SHA 见 [交付记录](result-review/installation.md)。

本版也包含 0.2.1 之后的英文长录音尾句修复：仅对符合条件的最后一段尝试重识别，保留完整前文，恢复结果等待确认。固定回归中一个公开长音频尾句改善，其余样本没有字符退化；不代表自然口述准确率普遍提升，详见 [逐样本验证](late-tail-verification.md)。

### 安装与升级

这是源码正式版，不提供预编译二进制、模型或可选 Fun worker。可下载 [源码包与 SHA256SUMS](https://github.com/mainliufeng/hyprvoice/releases/tag/v0.3.0)，或固定版本克隆：

```sh
git clone --branch v0.3.0 --depth 1 https://github.com/mainliufeng/hyprvoice.git
cd hyprvoice
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/hyprvoice --version
```

依赖、模型路径与首次启动见 [README](../README.md)。升级前完成或取消待处理结果，备份旧程序，保留现有配置和模型。F9 松开绑定需从旧的 `hyprvoice stop` 更新为 `hyprvoice command-release`；按下仍使用 `hyprvoice command`。示例见 `config/hyprvoice.conf` / `config/hyprvoice.lua`，二选一合入自己的快捷键配置。不要同时加载两份绑定。

### 验证范围与限制

0.3.0 冻结功能已通过 13 组 CTest、45 项原语音修改回归、8 项实际私有 PipeWire 故障检查、70 项恢复/候选/草稿/重试检查，以及独立主桌面 15 项、多 seat 29 项检查。部分故障使用明确的 Audio/ASR 替身或本地 HTTP 合约；这些数字包含跨组重复场景，不能当成真实口述质量分数。最终公开 WAV 的真实 X-ASR 回放与同模型基线逐字、前缀、尾句及评分一致，详见 [验收记录](result-review/verification.md)。

正式发布另外从干净 checkout 与将要上传的源码包执行 Release 构建、完整 CTest、无配置版本查询及包校验，结果见 [发布核验](releases/0.3.0-verification.md)。仓库未配置 GitHub Actions；本次自动测试在本机执行。

真实麦克风口述与外部文字模型的语义质量没有验收。既有中英词改写、英文中间句遗漏及 Fun-ASR 否定错误仍可能出现；候选和差异只提供检查线索，不保证哪一版正确。没有声称对标商业输入法。

## 0.2.1 · 2026-10-08

中英混说时，如果录音过程中和结束后的两次识别对局部英文词有不同结果，面板会提示“英文词可能听错了，请检查后再点‘输入’”，保留文字并等待确认。正常识别仍按原设置自动输入。这个提示不能判断哪次识别正确，也不会自动改词。

- **确认后输入**：只有检测到上述分歧时暂停自动输入；点“输入”将显示的文字送入输入框，点右上角关闭取消本次结果。
- **失败后可重试**：文字处理失败时同时说明失败与英文词存疑，保留本次文字；重试成功仍需要确认，不重复输入。
- **保留已有兼容**：标准 Hyprland 和 Lua 快捷键接口适配、Chrome／独立 ChatGPT／Ghostty 输入、X-ASR 与可选 Fun-ASR、前文辅助和设置沿用 0.2.0。识别分段、模型、热词及配置默认值未改变。

### 安装与升级

从源码构建，依赖和首次配置见 [README](../README.md)。

```sh
git clone --branch v0.2.1 --depth 1 https://github.com/mainliufeng/hyprvoice.git
cd hyprvoice
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/hyprvoice --version
```

升级前完成或取消待处理的语音结果，备份旧程序，保留原配置与模型。新提示不需要改变后端或云端上下文范围。

### 验证范围与限制

2026-10-08 本机真实模型回放共 350 条结果：X-ASR 原127条、冻结A/B各45条及各3条拼接长音频，Fun-ASR 原127条。识别文字全部与 0.2.0 逐字相同；X-ASR 原集 CER 6.81%、Fun 原集5.13%，前后不变。X-ASR 在原集和A/B各2条出现新检查提示；Fun 未出现提示。样本含相关副本及已用于回归的反例，不是350条独立盲测，也不代表自由口述质量。正确的英文词也可能需要检查；共同错词、纯英文遗漏或否定错误不一定被提示。这版没有修正这些识别质量问题。

新增隔离界面流程26项检查通过，覆盖提示可见、暂停自动输入、显式输入一次、取消与重新录音、无分歧正常自动输入、大小写／标点不误报、文字处理失败／空结果／超时后重试恢复、取消后迟到响应和重复操作。界面验收使用生产 App/UI 对象、私有 ASR 边界适配器的受控响应、零样本音频、私有 GTK 输入框及本机模拟文字处理接口；真实模型回放另行执行，不混称真人或真实麦克风验收。

用户于2026-10-08确认日常 Chrome、独立 ChatGPT 和 Ghostty 均能直接输入；这是0.2.0输入兼容的用户反馈，不是新增提示的真人验收。新增提示的实际口述体验、物理麦克风质量评估与真实LLM仍未验收。发布构建与 CTest 另行核验。

## 0.2.0 · 2026-10-07

这一版在现有本地语音输入上补齐文字处理失败恢复、输入位置保护、轻量设置，并修复普通听写和快捷键发送兼容问题。保留 X-ASR、可选 Fun-ASR、前文辅助和原文回退；没有替换本地识别模型。

- **直接输入**：普通听写在无法提供编辑器信息的应用中仍可送入原窗口；终端使用 Ctrl+Shift+V。自动适配标准 Hyprland 与支持 Lua 快捷键接口的 Hyprland。悬浮条不假定实际快捷键一定是 F8/F9。
- **更清楚的操作**：“输入”将面板中显示的文字送入输入框；“撤销修改”只切回识别原文，预览不会自行输入。发送结果不确定时保留文字、提醒检查输入框并提供复制，不重复发送。
- **本次文字重试**：文字处理失败后可“再试一次”，复用本次识别文字，无需重新录音。选区指令失败不替换选区、不插入口述指令，重试结果仍需确认。
- **输入位置保护**：对可核验编辑器检查控件、光标、选区、内容及录音期间的变化，防止插入已变化的位置。未知编辑器仅核验原窗口，不读取前文或向文字处理接口发送内容；无法检测同窗口内未暴露的光标、密码或选区。
- **设置与诊断**：空闲时打开 `hyprvoice settings`，调整场景、识别后端、自动输入和前文开关。`hyprvoice diagnose` 提供不录音、不联网的本地检查。
- **自定义场景**：配置中的 `prompts` 场景名直接显示，不再只支持四个内置标签。默认纠错／整理提示词强调保留数字、否定、名称和原意。
- **版本查询**：`hyprvoice --version` 返回软件版本，不读取配置或启动录音。

### 安装与升级

仍从源码构建，无预编译二进制包。构建依赖、实际模型配置与首次运行见 [README](../README.md)。

```sh
git clone --branch v0.2.0 --depth 1 https://github.com/mainliufeng/hyprvoice.git
cd hyprvoice
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/hyprvoice --version
```

已有安装更新时先完成或取消待处理的语音结果，保留旧程序和配置，再替换程序并重启 Hyprvoice 服务；不需重启输入目标应用。先在日常应用验证直接输入，再移除旧备份。程序不会自动迁移或覆盖配置、切换默认后端或扩大云端上下文范围。

### 验证范围

输入逻辑基于 `8f0f0d8`：2026-10-07 七组隔离桌面流程共 321 项检查通过，包含跨组重复用例；CTest 9/9 通过。其中四组在当前特性分支同一 Hyprland 程序上运行，三组在标准 Hyprland 上运行。Chrome、Electron 42 同类独立窗口、Ghostty 验证了实际收到文字、精确一次且不提交消息。发送后报错保留结果、禁止重发及显式复制亦验证。

入口在 `tests/browser_desktop_test.py`、`tests/window_input_cases.py`、`tests/result_actions_cases.py`、`tests/retry_desktop_test.py`；测试使用私有桌面、公开固定文字、零样本音频及回环模拟接口，没有真实麦克风或外部模型调用。版本查询及构建另做发布检查，不改变上述输入逻辑。

真实日常 Chrome、独立 ChatGPT、Ghostty 与真人录音验收仍待确认；Electron 同类夹具不等于实际 ChatGPT 应用已验收。既有 ASR 固定集结果也不代表自由口述质量。未声称支持其他桌面或质量已经对标商业输入法。

详细说明：[结果操作](result-input-flow.md)、[浏览器输入](browser-input.md)、[重试](retry-verification.md)、[目标保护](target-protection.md)、[保真策略](fidelity-verification.md)、[设置与诊断](settings-and-diagnostics.md)。
