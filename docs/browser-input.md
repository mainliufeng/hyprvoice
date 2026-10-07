# Chromium 网页输入框

语音输入现在可识别 Chromium 网页中的普通 textarea 和 contenteditable 聊天输入框。沿用自动输入设置；需要检查结果时，点击“输入”即可输入当前显示的文字。程序只输入，不发送聊天消息。密码框、只读控件、地址栏和终端不作为普通网页输入框。

目标先匹配桌面窗口 PID，再在该应用内查询焦点，并要求处于活动 FRAME 和 DOCUMENT_WEB 下。Collection 的焦点查询避免遍历整段聊天历史。首次查询通过标准的应用属性请求启用 Chromium 懒加载的网页无障碍树，不增加浏览器启动参数，不修改全局无障碍设置，不重启浏览器。

内容核验只读取已确定的可编辑控件。Chromium 的富文本父控件可能只返回 U+FFFC 占位符；`src/browser_text.cpp` 沿同一控件的 Hypertext 链接展开其段落，核对节点归属、角色、文本版本、真实光标与选区，并记录摘要。先检查所有链接节点的密码角色，再读取文字；范围与资源预算不足时拒绝输入。段落正文、光标和选区变化都能使原目标失效，即使总长度相同或光标又移回原处。

上下文仍仅取当前输入框光标之前、现有 `max_chars` 范围内的内容；不会把网页聊天历史、地址或其他输入框加入上下文。底层校验在本地使用摘要，目标状态不包含正文。实际输入仍走已存在的精确剪贴板准备、目标复验和粘贴快捷键；不改变显示文字，不发送 Enter。选区替换通过 Document.GetTextSelections 的真实端点处理 Unicode 选区，避免 Text.GetSelection 对表情偏移重复转换；仍保留复制内容与已核验选区摘要匹配的要求。构建要求 AT-SPI 2.52 或更高版本。

隔离验证：`tests/browser_desktop_test.py` / `tests/browser_input_cases.py` 使用新 Chromium profile、公开本地 HTML、1200 个合成聊天历史节点、私有会话与 AT-SPI、独立 Hyprland、零样本 PipeWire 和 loopback HTTP。`tests/private_a11y_locator.py` 仅向私有测试会话公布其 AT-SPI 地址。模拟识别只返回固定公开文本，不读取真实麦克风、不复用日常浏览器 profile、不调用外部模型，也不打开 DevTools。测试观测真实 paste/input/submit 事件及公开测试框内容，验证一次插入和不自动发送。

仍需真人验收真实 ChatGPT 页面。Firefox/Gecko 未在此变更中支持；第三方编辑器缺少可信无障碍树时保留复制回退。粘贴前检查与跨进程快捷键发送不是原子事务，也没有应用接收回执；无法核验时保留文字并拒绝自动输入。
