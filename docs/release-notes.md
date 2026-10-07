# 版本说明

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
