# 公开语音回归集

`manifest.jsonl` 包含 127 条真实公开语音、加噪语音、拼接长录音及无语音样本，沿用之前测试的输入和参考文字作为固定回归集。这里复用的是数据和元数据，没有复用旧评分、合成或执行代码。

- Google FLEURS，固定 revision `70bb2e84b976b7e960aa89f1c648e09c59f894dd`：20 条普通话、10 条英语及其衍生样本。许可 CC-BY-4.0；来源 https://huggingface.co/datasets/google/fleurs 。清单保留 split、row、source_id、语言和 hash。
- 列车声使用 PyTorch torchaudio tutorial asset 的 Daniel Simion 录音，CC-BY-3.0，具体资产 URL 与署名保留在含列车声的清单项中。
- 白噪声／静音为人工生成；加噪为确定性 SNR 变换。长录音由同一 fold 的公开录音拼接。不是用户日常录音，也不是微信／豆包结果。

WAV 不随源码提交。本机在 `build/corpus/` 保留这些固定输入，清单相对路径只指向这个目录。迁移时复制该目录中的 WAV 或按清单从公开源重新准备，并运行 hash 核对；不能用替代音频冒充相同测试集。模型配置也须指向已安装的真实模型。

```bash
HYPRVOICE_CONFIG=build/local-config.json build/hyprvoice replay \
  tests/fixtures/manifest.jsonl > build/results.jsonl
python3 tests/score.py tests/fixtures/manifest.jsonl build/results.jsonl \
  --verify-audio --baseline docs/asr-results.jsonl --output build/score.json
```

评分使用 NFKC 和 Unicode casefold，保留字母与数字后计算字符 Levenshtein 距离。无语音误插字单列；结果 ID 缺失、重复或多余均失败。历史结果也通过同一个评分器重算后比较。

这个小型固定集用于回归诊断，不代表自由口述、物理麦克风、全部噪音或竞品总体效果。本次在诊断过程中查看了所有 fold 的结果，因此 holdout 名称只保留数据分组含义，不能视为未见测试集。
