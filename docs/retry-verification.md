# 本次文字处理重试的验证

`hyprvoice retry` 和悬浮窗“重试文字处理”复用本次原转录、实际处理场景、原选区、原前文与 ASR 警告，不读取新的输入框内容，不重新录音或识别。待处理结果固定原目标窗口；提交继续检查窗口与选区。重试结果需要显式确认，取消、提交后快照失效。处理中的重复操作被拒绝；取消与结果发布在同一锁内检查会话版本，完成后才接纳下一轮录音。

## 自动测试

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPRVOICE_WITH_FUN_ASR=OFF
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

CTest 包含 core、ui_text_config、benchmark_metrics、fun_worker_contract、rewrite_http_contract、retry_http_contract。后者通过 `retry_probe` 调用生产 `ProcessText` 与 `Rewrite`，本地 HTTP 和固定合成输入验证 16 项情况：成功、HTTP 500、空/NULL/非法 JSON、ASCII/Unicode 空白、超时、预取消/进行中取消、失败与取消后的重试、命令模式成功/失败/重试、ASR 警告保留、重试正文逐字节一致。普通失败回退到原文，指令失败输出为空，不能用原指令替换选区。

## 隔离桌面验收

`tests/retry_desktop_test.py` 必须由独立 Hyprland/GTK/AT-SPI 会话运行，传入私有 runtime 和证据目录；拒绝日常 runtime。依赖现有 `test_editor`、`test_keyboard`、GI/Gio、PipeWire CLI 和 grim，不自动安装或重启任何服务。开发工作区的 runner 创建独立 D-Bus、Sway/GLES2、嵌套 Hyprland、唯一 HV-QA headless 输出及私有 AT-SPI bus 后调用：

```sh
/usr/bin/python3 tests/retry_desktop_test.py "$XDG_RUNTIME_DIR" /absolute/private-evidence
```

脚本新建仅含虚拟零源的 PipeWire，断开日常音频；测试 worker 检查 WAV 全部样本为零，返回固定合成转录。文字处理仅访问随机 loopback HTTP 端口；GTK 编辑器内容完全为合成数据。通过 AT-SPI 激活实际按钮，覆盖 36 项检查，包括：

- 首次失败保留原文；重试不再运行 ASR；编辑前文后重试请求仍与首次逐字节一致。
- 重试按钮禁用、重复重试与处理中新录音/提交被拒绝；默认开启自动提交时，重试成功仍等待手动确认。
- 真实编辑器只插入一次，重复提交或已提交后的重试被拒绝；粘贴已发出后的可选剪贴板恢复故障不能让结果重新可提交。
- 空结果、超时保留原文；原文回退实际只插入一次。
- 取消立即清空本次内容，直到请求收尾才能新录音；迟到响应在下一轮录音期间返回，不改变下一轮状态或转录。
- 错误窗口和改变后的选区阻止提交；回到原窗口/恢复原选区后允许确认。指令失败不显示原文按钮，成功只替换选区一次。
- 密码控件前文为空，不创建可重试的 HTTP 请求。

成功运行保存请求 SHA256、检查结果及实际界面截图，不保存任何日常用户内容。脚本结束停止所有自建子进程。

## 覆盖边界

默认构建编译生产 FunBackend 与 X-ASR 集成；Fun 合约与上述桌面测试使用显式测试 worker，不代表真实 Fun-ASR 质量通过。`HYPRVOICE_WITH_FUN_ASR=OFF` 不构建 Fun-ASR/llama.cpp 的可选 `fun-worker` 目标。本轮未下载该目标依赖或运行真实模型、麦克风、外部模型及真人验收。

桌面粘贴协议没有应用完成回执；本轮证明合成 GTK 编辑器和生产状态机发出一次交付，不能据此宣称所有日常应用均已验收。同一窗口内光标移动的进一步目标保护属于后续独立工作。
