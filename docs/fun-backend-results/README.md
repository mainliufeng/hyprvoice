# Fun 后端接入验收数据

`native-replay.jsonl` 是正式识别接口重放固定 277 条公开录音的逐条输出，`equivalence.json` 核验它与先前固定版本 Fun 候选的文字一致。`accuracy.json` 仅汇总保真分数；该次重放与研发负载重叠，逐条时间不能替代空闲主机性能测评。

`desktop.json`、`context.json`、`pinyin.json` 来自独立真实 Hyprland 会话；`installation.json` 和 `installed-transcribe.json` 核验用户环境的安装与公开录音识别。没有日常前文、剪贴板、凭据、权重或编译二进制。

公开数据来源为 ASCEND（CC-BY-SA-4.0）与 FLEURS（CC-BY-4.0），派生文字遵循对应数据许可；录音身份、固定 revision、来源链接与许可见 [原测评清单](../benchmark-20261004/screen-cases.jsonl)及 [数据说明](../benchmark-20261004/README.md)。此目录保留数据归属；代码许可独立。

文件完整性见 `SHA256SUMS`；功能、构建与第三方来源见 [Fun 后端说明](../fun-backend.md)。
