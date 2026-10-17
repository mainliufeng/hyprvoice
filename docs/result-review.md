# 结果保留与检查

实现基线为 be0f911，包含 main 的 a5d44d1 尾句修复及 7e82cc0、4cb409a、52f5ec4、25a058f 四项 Cornice 提交。原型 A 已经上级审查确认；本文件记录生产候选行为，安装状态以最终交付记录为准。

默认仅显示一份正文。实际取得实时、精修两个音频识别结果且有实质差异时，正文片段轻标记可点，点击后局部上下展示当前与备用来源；相同部分不重复。空片段以界面提示表示，复制／输入不会带上提示。标点与空白本身不生成候选，数字、小数、金额与否定差异保留。长文本采用有预算的文字对齐，必要时明确标为整段来源对照；这里的文字位置从未当作音频时间戳。

正文经文字模型、F9 或手动编辑后，旧 ASR 候选不能覆盖新版；来源结果仍保留。修改比较属于文字版本差异，通过“查看修改”显示增加、删除和替换，默认回到干净正文。没有第二个实际识别结果时不制造候选或置信度。

采集设备／PipeWire 连接出错、积压或时长上限后，先停止采集并排空有效队列，再尽量收尾 ASR；收尾失败则回退最后一个有效非空预览。恢复正文等待人工确认，不自动输入、不发送后续文字处理请求。不确定缺口位置时只提示整体可能不完整。没有可用结果则明确失败。主动取消丢弃本轮，和异常恢复分开。F9 修改要求录音中断则保留原正文，不发送部分指令。

“编辑正文”将整段正文置于输入框。草稿具有独立 session/base_revision，在“完成编辑”前不进入 effective_text；“暂不应用”保留有修改的草稿，复制、输入、F9、候选切换受门禁；可继续编辑或明确放弃。无修改退出不留过期草稿。手动保存后返回检查状态，再确认输入。

原目标窗口、PID、seat、控件与内容核验继续生效。浮层焦点不作为新目的地。primary 编辑在 layer-shell 按需键盘模式；Cornice secondary 编辑在原 seat list 映射的实际 Wayland display 上打开窗口，不给镜像浮层增加键盘接管权限。开始下一次 F9 时校验原目标并排空旧焦点事件，仅重置新一轮采集焦点标记。

## 共享结果与 UI 接口

复制、输入、F9 只取 `effective_text`。`raw` 是不可变的初次最终识别；`text` 是当前正文；`session_id/result_revision` 约束异步提交。

| 接口 | 数据／行为 |
| --- | --- |
| `edit-begin` | 进入编辑；恢复独立草稿 |
| `edit-draft` | JSON `{text}`，只更新草稿 |
| `edit-save` | 校验草稿 session/base_revision，保存正文并清草稿 |
| `edit-cancel` / `edit-discard` | 暂存退出／明确放弃草稿 |
| `select-candidate` | JSON `{session_id,base_revision,index,source}`，source 仅实际 streaming/refined |
| `check-target` | 只核验原目标可用性，不自动输入 |
| `revise` / `command-release` | F9 开始／松开停止修改要求采集 |

状态包含 `asr_streaming/asr_refined/asr_parts/asr_candidates_active`、`result_source/change_before`、`recovered/incomplete/capture_error`、`editing/draft_text/draft_pending/draft_session_id/draft_base_revision`。标记和警告不拼入正文。

## 针对性验收

- `result_review_test`：来源缺失、汉英混合、重复词、否定、数值、小数、两方向还原、纯正文与 markup escaping、长文本预算；Release 仍启用断言。
- `audio_capture_test`：向实际 PipeWire Process 回调注入 buffer，验证 10 秒队列满仍保存当前 buffer 有效前缀，停止不清队列，错误连接与正常停止分开。
- `HV_TEST_SUITE=review tests/run-revision-desktop.sh`：同一生产 App／GTK 加仅测试链接的 Audio/ASR doubles，验证一次收尾、失败预览、无预览、时长、F9 中断、候选、草稿和纯正文复制。不安装 recovery_app，不把测试后端用于用户路径。
- `HV_TEST_SUITE=capture ...`：实际私有 PipeWire 断开，生产 Audio + Fun worker 合约测试；无预览后端恢复、一次解码、禁止自动／模型、纯正文复制、无音频失败。
- 默认 revision suite 与独立 `tests/revision_focus` 分别验证旧回归、实际指针／键盘、Cornice seat 保护和两个 display 编辑生命周期。公开 WAV 的真实 X-ASR 回放用于同模型保真对比，不把合成模型 HTTP 通过称为真实模型质量验收。

全部桌面测试使用私有 runtime、HOME、bus、PipeWire 与 loopback，未使用日常麦克风、输入框或私人文字。最终 SHA、具体通过项和运行截图随最终验证记录保存；本文件不代替安装或真人体验验收。
