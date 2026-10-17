# 0.3.0 结果检查验收

生产源码冻结于 `3730ecb`（包含 `e759cce` 功能、`91b15a9` 候选高度、`9673db3` 重试来源及消费后清理）。Release 二进制 SHA256：

`37709bdf4b6cad1a3cd047c1e272f24be676ebfda1949dbc22b026abbfe82612`

仅测试链接的 recovery_app SHA256：

`b68c131128c1e12291396b126b357cbf44c2371df11e93d5e8f32f1f30996e4a`

测试二进制使用同一生产 App/GTK，替换 Audio/ASR 以覆盖难以稳定触发的故障。它不安装，不代表真实 ASR 质量；[构建来源](evidence/binary-provenance.json)记录两者区别。后续仅文档与独立测试证据归档，不改变上述生产源码。

| 检查 | 结果 | 实际边界 |
| --- | --- | --- |
| CTest | 13组通过 | C++断言在Release启用；HTTP/worker均本地合约；[结果](evidence/ctest.json) |
| 原语音修改回归 | 45项通过 | 生产37709、实际GTK/PipeWire、ASR与HTTP替身；[结果](evidence/revision-app.json) |
| 实际采集连接故障 | 8项通过（含实际Label断言） | 生产37709、实际私有PipeWire断开/启动失败、Fun worker合约；[结果](evidence/capture-app.json) |
| 恢复/候选/草稿/重试 | 70项通过 | recovery_app b68c、Audio/ASR显式替身、实际GTK/指针/剪贴板；[结果](evidence/review-app.json) |
| 独立主桌面/多seat | 主桌面15项、多seat29项通过 | 同一生产37709；实际指针、键盘、两个secondary display生命周期、F9/撤销/一次输入与launcher保护 |
| 公开WAV真实X-ASR | 同模型对基线逐字、前缀、尾句及评分一致 | 同一生产37709；warning为空，这次未触发尾句恢复警告，不冒称触发验收；[独立归档](../../tests/revision_focus/evidence/release-3770/README.md) |

所有隔离测试均未接入日常麦克风、日常输入框或私人文本，没有外部模型请求。实际公开 WAV 回放只能说明该样本同模型不退化，不证明所有口述识别质量，也没有覆盖真实云端模型质量。

## 关键行为与实际截图

- [默认正文的实际ASR片段标记](evidence/asr-default.png)，[点击后局部上下候选](evidence/candidate-expanded.png)。截图来自明确的双来源测试数据；默认标记由实际指针命中，下候选经GTK动作选择，正文/原始来源/复制一致。无第二结果无候选，数字和否定不被标点过滤吞掉。
- [未应用草稿](evidence/draft-retained.png)、[保存后的正文](evidence/manual-saved.png)、[长文整段编辑](evidence/long-editing.png)、[长文阅读](evidence/long-reading.png)。UI只显示编辑动作，草稿不会被候选/F9覆盖；无修改退出不留过期草稿。长文在滚动区完整保留，动作区持续可见。
- [F9新版默认查看](evidence/voice-revision-reading.png)、[明确文字版本比较](evidence/voice-revision-comparison.png)。新增、删除、替换只表示版本差异，旧ASR槽停用；撤销由独立原生链验收。
- [文字处理重试后的正文](evidence/retry-processed-reading.png)。首次失败后重试同文保留实际ASR候选，改文则原子失效旧槽；拒绝旧候选覆盖，实际Label与复制内容一致。
- [实际PipeWire断开恢复](evidence/actual-pipewire-interruption.png)、[无可用音频的失败](evidence/actual-pipewire-start-failure.png)、[消费恢复结果后的复制反馈](evidence/recovered-copy-feedback.png)。前两张使用生产Audio和受控Fun识别响应。恢复等待确认、无自动输入/HTTP，复制只有正文；复制后清理陈旧不完整警告。

录音错误、积压、停止同时发生的错误、收尾失败、最后非空预览、无预览后端、无结果、时长上限及F9不完整指令分别覆盖。实际Audio Process回调测试还验证队列满时保留当前buffer可容纳前缀；测试不声称恢复已丢弃的音频。没有音频对齐时仅整体警告，从未把故障时刻冒充具体错字位置。

主分支已集成并推送，已安装同一37709生产二进制；详见 [安装与回退记录](installation.md)。
