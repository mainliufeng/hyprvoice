# 连续录音丢前文修复（2026-10-04）

X-ASR 的流式识别与结束后的离线精修使用同一份录音，但此前短录音（不超过 30 秒）的精修只收到 VAD 首尾检测范围内的音频。弱音开头或结尾可能已被流式模型识别，却没有被 VAD 检出；精修结果随后覆盖预览，导致文字消失。长录音此前已经保留全部样本，因此问题并非只有超过 30 秒才出现。

## 修改

- 所有长度的录音都完整进入精修。VAD 仍用于拦截无语音录音和提名长录音切点，不能删除录音首尾。
- VAD 的输入单独做有上限的音量归一化：目标 RMS 0.025、最多放大 8 倍、放大后的峰值不超过 0.8。原始录音和 ASR 输入不变，避免流式模型能识别弱音、VAD 却把整段判为空的矛盾。
- 如果 VAD 仍未确认语音，保留真实录音阶段已经形成的至少 4 字符转录，显示确认提示并阻止自动插入；不再直接清空长句。零输入补帧产生的孤立字符和不足 4 字符的疑似噪音碎片不保留。这是待确认结果，不冒充已确认讲话。
- 对至少 20 个字母、汉字或数字的流式结果，如果精修输出的这些字符不足原结果的 85%，保留流式结果，并记录不含文字的原因。标点、空格不计入；短数字转换不触发这道保护。
- 本地诊断日志只记录收到的音频时长、音量、识别字数及 VAD 时间范围，不保存录音、不记录转录或输入框内容。

缩水保护是异常检测，不能证明每句话都正确，也不能检测长度接近的替换、幻觉或轻微漏字。它作用于 ASR 精修，不作用于用户主动选择的翻译、整理或选区修改。

## 真实复现与验收

公开 Google FLEURS 的普通话 validation-1（7.44 秒）和 validation-2（7.56 秒），各加一秒尾部静音，总长 17 秒。将第一条 PCM 振幅乘以 0.01，保留第二条原始振幅。原始文件 SHA256 在准备时核对，来源和参考文字见 [固定回归集](../tests/fixtures/README.md)。

- 修复前：流式结果包含“因为远离大陆”，共 69 字符；离线结果裁掉开头，只剩 56 字符，以“长度跋涉而来”开始。
- 修复后：相同录音输出 70 字符，保留两条句子。反向排列、弱音放在结尾，以及两句都为弱音的情况同样保留两条句子。归一化后弱音前文的 VAD 起点从约 2.486 秒提前至约 0.406 秒。
- 隔离 Hyprland 的真实 PipeWire 回放、GTK 预览和粘贴：旧二进制丢失开头，新二进制保留首尾；弱音首尾共 14 项检查通过。
- 真实 AT-SPI 输入框前文与 DeepSeek 处理：仅发送公开测试前文及公开转录；10 项检查通过，完整新增文字实际出现在编辑器中。
- 73 秒中文录音的首、中、尾六个内容标记经真实录音和粘贴完整保留。
- 静音、白噪声和列车背景声三条固定样本仍无输出。
- CTest 四组通过；真实模型的正常、音量匹配、弱音开头、弱音结尾、全段弱音五条回归通过。

- 本机扬声器播放公开录音，由真实数字麦克风收音，在隔离 Hyprland 编辑器中模拟按住/松开 F8 的同一 press/release 路径：松开后自动插入，两句的开头、内部和结尾标记保留，7 项检查通过。全局扬声器 55%、麦克风 50% 不变；测试播放流使用 200%，第二条源录音先匹配音量（振幅乘 3.8）。未匹配音量的较低回放实测只识别第一句，说明收音可听度仍影响结果。未保存麦克风录音。
- 上述物理回放 ASR 原文有“科隆”变“克隆”、重复“主”等错词；该次字符错误率为 3/61（4.92%，不把数字写法视为等价），不能把内容标记完整当作逐字正确。
- 单独注入仅测试使用、固定判无语音的 VAD 分类器，语音识别仍使用真实 X-ASR：悬浮窗保留两句并显示待确认，编辑器不自动写入，手动确认后完整粘贴，9 项检查通过。测试模型没有安装到日常服务。
- 固定 277 条公开样本，保持 X-ASR 模型、热词、样本与评分条件：仅 ASR 的字符错误率由 1236/11413（10.83%）降至 1008/11413（8.83%），无运行失败或无语音样本误插字。详见 [固定集评分](continuous-recording-regression.json)，这不是微信或豆包的对照分数。

机器可读的公开结果见 [continuous-recording-results.json](continuous-recording-results.json)。原始手机录音不可取得，因此未声称重放了用户那条原音频；本机物理回放已经独立验证真实收音、松开及自动插入链路。

## 复现命令

先按固定回归集说明准备真实 WAV 和模型，再运行：

```bash
python3 tests/continuous_check.py /path/to/config.json build/continuous-check
# 对照某个已安装或旧版二进制：
python3 tests/continuous_check.py /path/to/config.json build/continuous-baseline \
  --binary /path/to/hyprvoice
```

桌面验收必须使用隔离的原生 Hyprland 会话，不能操作用户日常桌面。测试会创建并清理专用 PipeWire 音源：

```bash
python3 tests/desktop_check.py /path/to/config.json \
  build/continuous-check/quiet-prefix.wav build/corpus/white-no-speech.wav \
  build/continuous-desktop --continuity-only
```

也可在同一隔离会话中指定真实声学设备（需提前允许扬声器播放）：

```bash
python3 tests/desktop_check.py /path/to/config.json \
  build/continuous-check/level-matched.wav build/corpus/white-no-speech.wav \
  build/continuous-hardware --continuity-only \
  --hardware-source <microphone-node> --hardware-sink <speaker-node> \
  --hardware-volume 131072 --binary ~/.local/bin/hyprvoice
```

误判路径的可重复故障注入依赖可选 QA 包 `onnx`，只用于测试：

```bash
python3 tests/vad_failure_check.py /path/to/config.json \
  build/continuous-check/normal-continuous.wav build/vad-reject-test \
  --binary ~/.local/bin/hyprvoice
# 隔离 Hyprland 中再运行 desktop_check，使用生成的测试配置：
python3 tests/desktop_check.py build/vad-reject-test/config.json \
  build/continuous-check/normal-continuous.wav build/corpus/white-no-speech.wav \
  build/vad-reject-desktop --continuity-only --vad-reject-only
```

不要把 `TEST_ONLY_reject_speech.onnx` 或生成的测试配置安装到日常服务。

现有 X-ASR/Fun 选择、快捷键、180 秒默认时长限制和用户设置不变。此修复针对 X-ASR 的应用内 VAD 与精修路径，不声称改变 Fun 原生运行时的 VAD 或模型识别能力。
