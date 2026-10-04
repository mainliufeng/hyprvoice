# 2026-10-04 实测证据

结论与解读见 [测评报告](../benchmark-20261004.md)，复现与评分定义见 [协议](../benchmark-protocol.md)。

- `*-cases.jsonl`：可移植的录音身份、公开参考与音频 SHA，不含音频文件或本机绝对路径。运行清单由 `benchmark_prepare.py` 从固定数据版本生成。
- `*-screen.jsonl`：同一 277 条的真实识别输出；对应 `*-score.json` 包含分组和逐条编辑数。
- `*-full.jsonl` / `*-ascend-full.jsonl`：全部 1,315 条的识别结果。
- `*-workflow.jsonl`：同一 150 条（120 干净＋30 加噪）的实际文字处理结果；`*-full-workflow.jsonl` 是全部 1,315 条。`raw` 是处理前的识别文字；失败保留原文且 `auto_commit_blocked=true`。
- `current-streaming-*`：正式识别程序同时输出的流式终稿，供精修阶段的质量对照；没有单独计时，因此速度字段为未知。
- `qwen*-long-fullcontext*`：四条已有长录音的额外整段模式诊断，不混入主比较成绩。
- `sources.json` / `verified-weights.json` / `runtime-build.json`：官方数据及模型版本和哈希、外部源码 commit、编译条件。运行时源码和权重留在忽略的构建目录。
- `hardware.json` / `current-assets.json` / `workflow-settings.json` / `*.runtime.json`：主机、当前模型/热词的哈希、实际提示词配置、并发与失败运行状态。API 密钥未保存。

ASCEND 原始数据由 Holy Lovenia、Samuel Cahyawijaya、Genta Indra Winata 等作者发布，见 [ASCEND 数据集](https://huggingface.co/datasets/CAiRE/ASCEND)及[论文](https://aclanthology.org/2022.lrec-1.788/)。本目录中来自 ASCEND 参考及音频的转录、身份清单和修改文本按 **CC BY-SA 4.0** 分发；FLEURS 衍生部分见 [FLEURS](https://huggingface.co/datasets/google/fleurs)，按 **CC BY 4.0** 分发。`source` / `category` 字段标明数据来源。本应用代码许可证不替代数据许可证。

这些是实际测得的快照，不是论文榜单、厂商数字、生成的假输出或真实用户日常语音记录。模型别名及网络状态可以变化，再运行的文本结果不保证逐字相同。
