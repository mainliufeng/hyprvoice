# HyprVoice 结果检查原型 v2

状态：**用户委托上级审查并确认 A「雾白靛蓝」，已进入生产实施与最终验收。** B「暖纸松绿」保留作对照。原型仅演示数据，生产验收独立记录。

本机入口：[默认阅读](http://127.0.0.1:8769/v2/?theme=indigo&state=read)、[两方案校对对照](http://127.0.0.1:8769/v2/?theme=compare&state=asr)。本机临时静态服务器仅绑定127.0.0.1，日志 `/tmp/hyprvoice-review-v2-http.log`，PID文件 `/tmp/hyprvoice-review-v2-http.pid`，没有修改系统服务。

若需重新启动，可运行：

```sh
python3 -m http.server 8769 --bind 127.0.0.1 --directory docs/result-review-prototype
```

## 可点击内容

- 两视觉方向和五个状态标签：阅读、ASR校对、编辑、中断恢复、语音修改。
- 当前/备选均可确认；逐处导航、来源、阅读/校对切换。
- 草稿暂不应用、恢复、刷新保留、完成编辑；空白保存禁用。
- 任意结果入口的修改要求、处理、失败保留、多轮修改、版本比较和撤销。
- 四种异常演示、收尾时保留/增长、无预览无结果时禁用输出。
- 目标变化拦截、图标名称/提示、Tab焦点顺序、Esc收起校对。
- 输出检查区只展示当前纯正文。复制按钮调用浏览器剪贴板，确认输入只到页面内的演示接收区。

正文、来源、目标及处理过程都是明确标注的固定演示样例，没有录音、模型调用或真实输入。实际F9快捷键未绑定；请点击按钮体验，以免触发机器上的运行版。页面状态保存在该origin的localStorage；“重置演示”清除本原型状态。

## 图像交付清单

| 文件 | 内容与状态 |
| --- | --- |
| `screens/two-directions-1280.png` | 1280视口原始双方案校对截图 |
| `screens/two-directions-1920.png` | 1920视口原始双方案校对截图 |
| `screens/two-directions-changes-1920.png` | 两方向的多色文字修改比较 |
| `screens/01-reading-panel.png` | 最终紧凑默认阅读近景，584×285，来自协调线程独立截图 |
| `screens/02-asr-panel.png` | 差异展开，当前/备选及来源近景 |
| `screens/03-editing-panel.png` | 完整正文输入与草稿状态近景 |
| `screens/04-interruption-panel.png` | 录音中断、整体提醒与保留文字近景 |
| `screens/05-voice-changes-panel.png` | F9文字比较的多色标注、撤销近景 |
| `screens/06-voice-requirement-panel.png` | 原正文与本次要求分开显示 |
| `screens/07-voice-processing-panel.png` | 修改中，保留旧正文和取消动作 |
| `screens/08-voice-failure-panel.png` | 本次失败，标题/状态明确保留上一版 |
| `screens/01-reading-1920.png` 至 `08-voice-failure-1920.png` | 对应状态的原始整页布局证据 |
| `screens/focus-visible.png` | 实际Tab焦点状态 |
| `screens/capture-metadata.json` | 整页截图时量测的552 px面板及视口信息 |
| `screens/panel-metadata.json` | 近景截图的浏览器clip几何数据 |
| `five-states.png` | A五状态与下一轮口述的六格总览，原始近景拼版，无拉伸 |
| `v1-v2-comparison.png` | 旧版与新版默认浮层并置，均保持原始比例 |

近景为浏览器直接clip PNG，保留少量外边距，不含原型工具栏；没有拉伸/上采样。1920/1280指实际CSS视口，不冒充2倍密度；实际截图像素与DPR记录见QA与metadata。

## 代码、检查及接口

`index.html` / `style.css` / `prototype.js` 为独立原型；`icons/` 来自官方 Lucide，原许可证在同目录。`design-review.md` 记录旧版问题、设计决定和推荐理由；`design-qa.md` 记录实际截图与检查；`interaction-checks.json` / `contrast.json` 为观察和计算记录。

继续沿用既定结果契约：`effective_text` 为复制、输入、F9的共同来源；`session_id/result_revision` 保护异步更新；草稿独立；`revision_instruction` 不进入正文；异常状态不因编辑或F9成功而消失。生产逻辑另在线程的候选分支，本原型没有接入它，也不声称ASR恢复或原生输入已完成。

原型分支仍为 `codex/result-review-prototype`，保留四个 Cornice 提交；这里没有集成 `main` 的尾句修复。后续生产集成应同时保留 `a5d44d1` 和四个 Cornice 提交，不能用本分支覆盖运行安装。
