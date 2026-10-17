# 语音修改逻辑版验收（2026-10-17）

本分支是可审阅逻辑版。完整“语音修改/撤销本次修改/手动编辑”生产UI仍待用户确认原型后接线，不能将接口测试说成最终UI交付。

## 已验证

- Debug构建通过；11组CTest通过，包含main英文尾句保留、回放评分、HTTP请求、重试回退、Fun worker、输入目标保护。语音修改HTTP合约新增空/空白/Unicode空白/仅零宽/NUL/null/空choices/非法JSON八类无效输出，全部保留原正文。
- 原普通听写、失败重试、密码/只读/选区/控件/目标保护：[60项回归通过](evidence/dictation-regression.json)。旧故障替身已适配Cornice seat粘贴命令。
- [隔离App结果](evidence/revision-app.json)：45项通过。生产App、真实GTK编辑器、真实PipeWire虚拟零源；ASR与HTTP为明确测试替身。
- 修改成功清除过时处理失败提示，原始recognition_warning独立保留；撤销仍恢复之前警告。
- 成功、多轮、两次撤销恢复手动正文；选定原识别/手动保存为修改来源；过期手动保存拒绝；修改请求仅包含original_body+revision_instruction。
- HTTP500、空输出、超时、无语音、识别worker报错、实际PipeWire启动失败，正文和raw都保留。
- 录音取消和HTTP取消恢复正文；旧响应晚到不能覆盖取消后手动正文；busy禁止重入/保存/提交。
- 自动输入开关开启时修改成功、撤销仍留在ready；F8/F9相互误按/松开不结束对方录音。
- 错误窗口无法输入/重绑；应用新选区不能覆盖；回原窗口明确输入一次，要求未被输入；提交后不能继续改旧结果。
- 已查看真实浮层截图：[新版预览](evidence/revision-preview.png)、[采集失败保留](evidence/capture-failure.png)。原窗口内容的变化仅由最终显式输入动作发生。

## 可复现

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DHYPRVOICE_WITH_FUN_ASR=OFF
cmake --build build -j3
ctest --test-dir build --output-on-failure -j2
HV_TEST_HYPRLAND=/absolute/path/to/compatible/Hyprland \
HV_TEST_EVIDENCE=/tmp/hyprvoice-revision-evidence \
/path/to/resource-scope.sh tests/run-revision-desktop.sh
```

隔离runner依赖现有Hyprland Lua构建、mutter、bwrap、renderD128、GTK/AT-SPI/PipeWire/grim。测试命名空间隐藏日常HOME、/run、/tmp、物理输入/声卡/card设备与系统总线，网络独立，仅暴露render节点。资源范围CPU200%、内存3G、进程512，由Cornice现有resource-scope.sh约束；无新增安装和后台服务。

普通听写回归可用相同runner设置HV_TEST_SUITE=retry；应用选区替换的旧测试已移除，由当前结果修改验收替代。

## 限制

没有录真实麦克风，也没有调用外部模型；此证据不证明ASR准确率或模型按自然语言改写的语义质量。防正文注入验证的是角色/字段边界，不能证明模型永远服从提示词。真实使用仍先展示由用户确认。

浮层新按钮、未保存草稿策略、候选校对与整段编辑UI由并行原型确认后的步骤完成。现阶段F9/CLI可审阅验证，旧“撤销修改”按钮仍是raw/processed来源切换，undo-revision才是语音修改撤销接口。

现场服务/绑定未改，未安装、未重启。运行0.2.1的SHA不足以证明精确源码提交；候选分支必须同时包含a5d44d1及全部4个Cornice提交，源码祖先/逐文件差异另核验。
