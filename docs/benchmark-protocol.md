# 本地语音工作流 benchmark

这套工具测试真实的“公开录音 → 识别系统 → 可选文本处理 → 最终文字”。输入法界面、录音设备、焦点保护、粘贴和输入框上下文获取不在离线评分范围内，它们仍须通过桌面验收。模型可以带自己的 VAD、分段、语言模型解码器；外置文本模型也可以参与，但必须分别报告处理前后结果、失败和耗时。

## 固定测试材料

- ASCEND 官方 test split：固定 revision `737e9800ae31be9932ba8464c80366559bd28424`，全部 1,315 条。它是中英自然对话，包含许多很短的片段，不等同于长段口述。
- 候选筛选：先固定随机种子 `20261004`，在中文、英文、中英混说中各抽 40 条 2–20 秒录音，再从这 120 条抽 30 条添加 10 dB 白噪声。不同引擎收到相同 WAV，逐条保存 SHA-256。噪声是可复现的人工变换，不代表真实房间、风噪或多人交谈。
- 旧 FLEURS 回归：本应用现有 127 条，包含朗读、不同噪声、拼接长录音和三条无语音样本。它们已被研发查看过，不能称为未见测试；同一原录音的加噪副本也不是独立样本。

筛选集合共 277 条。新的对话/噪声集合为 150 条；跨候选比较必须使用相同集合，不能把全量结果与抽样结果并排当成同一成绩。公开的预训练模型是否在训练中见过 ASCEND/FLEURS 未知，本工程没有用这些新测试输出调整提示词或解码参数。

## 前文与文本处理

`benchmark_prepare.py` 按 session、speaker、录音起点排序，只拼接同一说话人的先前公开参考转录，截取最后 1,024 个 Unicode 字符。此次固定 test split 的时间戳全部可解析，同一说话人的片段不存在时间重叠。当前与未来转录不会进入本条前文。

这相当于输入框已经有正确的键盘输入，是理想前文条件。它不是先前模型识别结果的滚动历史，也没有实测 AT-SPI 读取；因此不能据此声称已验证连续听写的错误累积或所有应用的上下文支持。

`workflow_probe` 调用正式 `Rewrite()`，使用既有 `correct` 提示词、实际 DeepSeek 配置和前文，仅当本次转录非空且存在前文时调用，符合当前 raw 场景的触发条件。`correct` 强制调用模式是另外的实验，不是当前默认流程。外部请求只含指定测试转录与公开前文，不包含录音、剪贴板或日常输入。

失败保留 ASR 原文，并记录 `rewrite_error` 和 `auto_commit_blocked`；正式应用此时需要手动确认。评分包括保留下来的原文，但必须另报失败率，不能称为全部自动上屏成功。不自动重试或删去失败样本。请求的 `deepseek-chat` 是服务商模型别名，不是不可变的模型版本。

## 评分与资源

- 中文主要看 CER；英文主要看 WER；混说主要看 MER（一个汉字或一个英文单词作为一个 token）。都是编辑距离除以参考长度，越低越好，不是满分 100 的质量评分。
- 使用 NFKC 和大小写归一；CER 只保留字母数字；MER 保留英文缩写/缩略形式的撇号。不做数字读法等价归一，也不忽略参考中的 `[UNK]`。因此这是同条件工程比较，不能直接复现或混用论文/榜单的数字。
- 参考保留口头词与重复词。删除口头词、改正语法、调整英文词形都会影响逐字成绩；CER/WER 不能单独评价整理后的可读性或语义保真。尤其不能把后处理的分数变差全部当成错误纠正失败。
- 无语音样本另报误插入次数；参考为空时错误率未定义。缺少、重复或额外的结果 ID 会使评分失败。没有计时的数据记为未知，不记为零耗时。
- 推理模型常驻，候选适配器另记初始化时间。候选 CPU 推理串行执行，使用 8 线程；当前安装的 X-ASR 按其生产配置使用 1 线程。Qwen 主比较固定 20 秒分段以约束长录音窗口；这与其 CPU CLI 默认的整段模式不同，额外诊断必须单列。离线计算时间和 RTF 不等于录音结束后上屏等待时间，尤其当前方案会在录音过程中运行流式识别。
- 原始 JSONL 保留每条时间；候选记录进程峰值 RSS，现装系统的进程峰值单列在 runtime 记录中。RSS 不包含其他应用、所有系统文件缓存或整机内存需求。测试发生在正常使用的笔记本上，不是独占的实验室主机。
- 若文本请求使用四个并发 worker，须明确记录；其每次请求时间不能当作单用户严格延迟保证。

## 复现

从仓库根目录执行。资产、环境、源代码归档、编译产物与凭据都留在被忽略的构建目录，不能提交模型或编译二进制。需要已有的 Hyprvoice 构建依赖，以及 OpenBLAS 的头文件和库。

```bash
export HV_BENCH="$PWD/apps/hyprvoice/build/benchmark-20261004"
export UV_CACHE_DIR="$HV_BENCH/uv-cache"
export UV_PYTHON_INSTALL_DIR="$HV_BENCH/python"
uv venv --python 3.12 "$HV_BENCH/venv"
uv pip install --python "$HV_BENCH/venv/bin/python" \
  -r apps/hyprvoice/tests/requirements-benchmark.txt
"$HV_BENCH/venv/bin/python" apps/hyprvoice/tests/benchmark_assets.py "$HV_BENCH"
"$HV_BENCH/venv/bin/python" apps/hyprvoice/tests/benchmark_prepare.py \
  "$HV_BENCH" apps/hyprvoice/tests/fixtures/manifest.jsonl
python3 apps/hyprvoice/tests/benchmark_build.py "$HV_BENCH" --openblas-prefix /usr
cmake -S apps/hyprvoice -B apps/hyprvoice/build -DBUILD_TESTING=ON
cmake --build apps/hyprvoice/build --target hyprvoice workflow_probe -j4
```

`--openblas-prefix` 也可以指向测试目录中独立解压的包。本机使用 Arch OpenBLAS 0.3.34-1；没有安装或更换系统库。构建脚本固定三个外部源码 commit，并检查提取的源码是否仍与归档相同。原生运行时是测试依赖，没有复制到应用源码或替换日常服务。

提供真实的 Hyprvoice 配置，在构建目录另存 raw 场景、禁用上下文的 ASR 测试副本；其他模型/VAD/热词配置与生产保持一致。将它赋给 `HYPRVOICE_CONFIG`，不要向文档或 Git 复制凭据。

```bash
HYPRVOICE_CONFIG="$HV_BENCH/current-config.json" \
  apps/hyprvoice/build/hyprvoice replay "$HV_BENCH/screen.jsonl" > "$HV_BENCH/current-screen.jsonl"
OPENBLAS_NUM_THREADS=8 "$HV_BENCH/qwen_benchmark" \
  "$HV_BENCH/qwen06" "$HV_BENCH/screen.jsonl" plain > "$HV_BENCH/qwen06-screen.jsonl"
OPENBLAS_NUM_THREADS=8 "$HV_BENCH/qwen_benchmark" \
  "$HV_BENCH/qwen17" "$HV_BENCH/screen.jsonl" plain > "$HV_BENCH/qwen17-screen.jsonl"
OMP_NUM_THREADS=8 "$HV_BENCH/fun_benchmark" \
  "$HV_BENCH/fun-nano/funasr-encoder-f16.gguf" "$HV_BENCH/fun-nano/qwen3-0.6b-q8_0.gguf" \
  "$HV_BENCH/fun-vad/fsmn-vad.gguf" "$HV_BENCH/screen.jsonl" > "$HV_BENCH/fun-screen.jsonl"
python3 apps/hyprvoice/tests/benchmark_score.py "$HV_BENCH/screen.jsonl" \
  "$HV_BENCH/current-screen.jsonl" --verify-audio --output "$HV_BENCH/current-screen-score.json"
```

用 `ascend-full.jsonl` 替换筛选清单即可跑完整 split。给 Qwen 适配器传 `prefix` 可以测试公开前文进入识别模型的额外实验；本文默认的 `plain` 不使用前文，也没有额外文本模型。

Qwen 适配器可选第五个参数为分段秒数，`0` 表示整段识别；省略为主比较的 `20`。改变分段是另一个系统配置，不能悄悄将最好的一次结果替换进主表。

仅在已授权发送对应测试文字时执行外部处理；`--env-file` 只读取配置指定的 API key 变量，不执行 shell，不访问剪贴板。默认一个 worker，本次批量测评使用四个。

```bash
python3 apps/hyprvoice/tests/benchmark_workflow.py \
  "$HOME/.config/hyprvoice/config.json" "$HV_BENCH/ascend-full.jsonl" \
  "$HV_BENCH/current-ascend-full.jsonl" "$HV_BENCH/current-full-workflow.jsonl" \
  --probe apps/hyprvoice/build/workflow_probe \
  --env-file "$HOME/.config/hyprvoice/llm.env" --workers 4
```

ASCEND 数据及由其转录产生的 benchmark 文本按 [CC BY-SA 4.0](https://huggingface.co/datasets/CAiRE/ASCEND) 归属原作者；FLEURS 数据按 [CC BY 4.0](https://huggingface.co/datasets/google/fleurs)。Qwen/Fun-ASR 模型为 Apache 2.0；外部 CPU 源代码的许可证保留在下载的源码归档中（Qwen C runtime 与 llama.cpp 为 MIT，Fun-ASR 为 Apache 2.0）。测试适配器与应用代码沿用本应用许可证。
