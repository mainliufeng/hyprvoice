# Fun-ASR-Nano 可选后端

此后端使用本地 CPU，F16 encoder、Qwen3 0.6B Q8_0 decoder 和 FSMN VAD。ASR 内部语言模型完全本地运行；原有 DeepSeek 文字阶段保持原配置，仅发送转录与必要前文。

## 安装与选择

```bash
cd apps/hyprvoice
./scripts/install.sh --with-fun
systemctl --user restart hyprvoice.service
hyprvoice backend fun
hyprvoice status
# idle / backend: fun 后使用原来的快捷键
# 切回：
hyprvoice backend x-asr
```

在没有录音或待提交结果时重启服务。普通 `install.sh` 只编译主程序，不下载 Fun。Fun 源码与 llama.cpp 在构建阶段获取，不运行 Python ASR 服务，不依赖旧 voice-input 项目。模型总大小以安装文件为准，安装时全部核验固定 SHA256；已有测评缓存可以重用，但必须通过同样的哈希核验。安装权重到 `~/.local/share/hyprvoice/models/fun-asr-nano`，worker 到 `~/.local/libexec/hyprvoice/fun-worker`。

配置示例：

```json
{
  "asr": {"backend": "x-asr"},
  "fun": {
    "worker": "~/.local/libexec/hyprvoice/fun-worker",
    "model_dir": "~/.local/share/hyprvoice/models/fun-asr-nano",
    "threads": 8,
    "timeout_seconds": 120
  }
}
```

`backend` 支持 `fun` 和 `x-asr`。切换命令先初始化真实模型，再原子保存后端选择；启动失败不覆盖原配置。保存只更改 `asr.backend`，不更改 DeepSeek、前文或其他配置。`doctor` 只检查所选后端需要的资产。

## 输入行为与资源

Fun 录音时只缓冲音频，结束后运行独立 FSMN VAD、最多 30 秒分段、贪心解码和每段 512 token 上限，与已测 CPU 离线候选一致。没有沿用 X-ASR 的 Silero 门槛。模型常驻子进程，多段录音复用权重；仅所选识别后端常驻。

录音期间不显示逐字文字，界面明确提示结束后识别。最终文字仍可预览、使用原文、参考光标前文、由 DeepSeek 处理或执行语音修改选区。录音中或待确认结果时禁止切换，避免丢失文字。快捷键、焦点变更防护、终端保护和拼音共存不改变。

音频通过权限 0600 的临时 IEEE float WAV 交给本机子进程，保留输入样本，处理结束、异常或取消后删除。取消识别或超时会杀死对应子进程、丢弃旧回复，下一次录音重新加载模型；正常多次录音不重复加载。重新加载前先启动麦克风缓冲，避免丢掉起始语音；重新加载阶段也可取消。默认识别超时 120 秒，不包含首次加载；首次加载上限 60 秒。无语音、worker 崩溃、超时或加载失败均不会自动插入文字或暗中切回 X-ASR。

`fun.threads` 控制语言模型解码线程；固定上游版本的 encoder 和 FSMN VAD 各使用 8 线程。

## 第三方来源

- [Fun-ASR 官方 CPU runtime](https://github.com/QwenAudio/Fun-ASR)：commit `0339018ba74a7defa3b6b6a96718d17b816be77b`，Apache-2.0。Hyprvoice 自写驻留进程适配器包含该外部 runtime，外部源码保留在构建目录。
- [llama.cpp](https://github.com/ggml-org/llama.cpp)：commit `8086439a4cea94c71a5dfb8fe4ad1546aebd640f`，MIT。
- [Fun-ASR-Nano GGUF 权重](https://huggingface.co/FunAudioLLM/Fun-ASR-Nano-GGUF)：revision `46e849502a867080d66d351b8dfb1018b607e509`，Apache-2.0。
- [FSMN VAD GGUF](https://huggingface.co/FunAudioLLM/fsmn-vad-GGUF)：revision `6840bae4c5c92ee8c04faaf4db23dd0105098d7f`，Apache-2.0。

固定源码归档 URL 与 SHA256 写在 `cmake/FunRuntime.cmake`，权重 URL 与 SHA256 写在 `scripts/fun_models.py`。安装复制上游许可到 `~/.local/share/licenses/hyprvoice/`；Hyprvoice 原有源码许可保持不变。接入没有使用 `apps/voice-input` 的代码。

## 2026-10-04 实际验收

- 正式 `Asr` → 本机驻留 worker 重放同一 277 条公开样本，零错误，277 条转录与之前固定版本 Fun 测评逐字相同。验证包括短语音、加噪、长录音与三条无语音。该次运行与编译、桌面 QA 有并行负载，其时间记录不作为空闲主机的性能承诺。
- `core`、`benchmark_metrics`、`fun_worker_contract`、`rewrite_http_contract` 四个测试组通过。Fun 合同测试覆盖浮点录音传递、异常退出、超时、加载错误与临时音频清理；仅这些合同测试使用模拟子进程，正式识别与桌面验收均使用真实模型。
- 独立真实 Hyprland 会话完成 32 项检查：加载 Fun、保存选择、拒绝录音/预览时切换、真实 PipeWire 录音、预览不抢焦点、Wayland/XWayland/Kitty 粘贴、识别中取消、纯噪音不插字、自动上屏、窗口变化保护、切回 X-ASR 等。
- 独立 AT-SPI 总线上完成 10 项真实前文检查：编辑器键盘前文与公开语音送入实际 DeepSeek，确认后粘贴、取消保留前文、自动追加新口述一次、清除上下文；另验证 Fun 服务运行时 Fcitx 拼音能输入“你好”。没有读取日常窗口或日常剪贴板。
- 实际查看录音与最终预览截图，黑灰配色保留，显示当前 `Fun` 后端，录音结束识别的提示与前文区域可见。
- 已安装新主程序、Fun worker、三份通过 SHA256 校验的权重与上游许可。空闲时重启日常服务，实际状态为 `idle / x-asr`，日常配置 SHA256 未变；已安装的 Fun worker 与权重另外成功转录公开 WAV，不依赖源码构建目录。

[逐条输出与验收记录](fun-backend-results/verification.json)及同目录的 `SHA256SUMS` 可核对上述结果；公开语音来源与许可沿用 [测试集说明](../tests/fixtures/README.md)和 [测评协议](benchmark-protocol.md)。这些结果不等于所有应用的兼容验收，也没有重新测量 Fun＋DeepSeek 在其他测试集的分数。
