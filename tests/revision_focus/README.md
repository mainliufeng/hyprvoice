# 独立焦点与公开尾句验收

所有权仅此目录。不要编辑校对线程的生产文件、既有tests或UI原型。

## 当前状态

- primary UI脚本已准备、仅语法检查通过，等待基于be0f911的新生产候选与可访问名称确认；未把它算成已通过验收。
- 使用现有private_desktop_runner同样的隔离边界和既有retry_desktop_test fixture，以独立bootstrap加载本目录cases，不修改旧fixture或生产App，也不通过edit-result绕过真实编辑控件。
- be0f911的recognition_coverage、replay_regression_guard、input_target_snapshot三组现有CTest再次通过。
- 公开long-en_us-dev音频已在旧本机corpus找到并核SHA。现有X-ASR真实回放be0f911末句完整；这次warning为空，**没有证明尾句修复分支实际被触发**；中间句遗漏仍在。

真实回放基线与模型SHA已保存于[evidence/be0f911-tail-report.json](evidence/be0f911-tail-report.json)，新候选对比时使用evidence/be0f911-tail.jsonl；同一样本两次实际回放逐字相同，评分/前缀检查通过。

## Primary真实浮层路径

```sh
HV_TEST_CANDIDATE=/absolute/production-candidate \
HV_TEST_HYPRLAND=/tmp/cornice-hyprland-fixed-20261016/build-release/Hyprland \
HV_TEST_EVIDENCE=/tmp/hyprvoice-focus-primary \
/home/liufeng/Code/self/cornice/test/resource-scope.sh \
/absolute/current-result-voice-edit/tests/revision_focus/run.sh
```

要求candidate已构建build/hyprvoice、test_editor、test_keyboard，保留现有tests/retry_desktop_test.py合成夹具。可访问名目前按已审原型：编辑正文、编辑完整正文、完成编辑、确认输入；生产名称变化由本独立测试适配，不能仅用旧CLI保存代替真实编辑。

cases覆盖：真实GTK TextView获取焦点→键盘写草稿→全局F9/输入均拒绝且草稿未提交→完成编辑→回原编辑器→F9→新正文→撤销恢复手动正文→其他窗口拒绝→回原窗口仍能再次F9→明确按钮输入到原控件一次。截图及检查JSON来自真实App；ASR/HTTP是合成替身，音频是私有零源，不代表真实麦克风或模型质量。

资源边界：既有scope CPU200%、MemoryMax3G、TasksMax512、RuntimeMax20min，PID/网络/用户目录/D-Bus/音频命名空间独立，仅renderD128可用；无物理输入、声卡、DRM card节点，无日常窗口与麦克风、无外部模型。

## Secondary / launcher真实夹具与当前结果

`run-native.sh`、`native_runner.py`、`native_bootstrap.py`、`native_cases.py`已实际执行现有兼容Cornice/Hyprland上的真实App。be0f911路由诊断模式16项通过后**整套失败**，证据见[evidence/native-be0f911-failed-checks.json](evidence/native-be0f911-failed-checks.json)。

- 确认actual physical-input-target-v2落agent1；真实App录音→F9→明确输入只改变agent1，primary/agent2/同seat其他窗口不变。
- 实际cn.launcher打开，F9与输入都拒绝且保字；不同seat、primary、同seat错误窗口均拒绝。
- 最后原seat/原窗口往返后的F9失败，原生route allowed=true但App旧token失效。这个失败已交生产线程修复，不能改成永远拒绝后宣称功能成功。
- 目视截图中HyprVoice浮层缺失。旧App连接legacy `$runtime/cornice/$signature/desktop.sock`不存在，真实broker在hashed ipc目录；端点已实际核查。修复需保留现有peer/PID权限。
- 当前broker的register-local-overlay固定keyboard=false，fork的keyboardOverlay要求rule.keyboard=true，仅GTK ON_DEMAND不足以在native takeover接键。未改Cornice接口或权限，实施线程评估既有secondary display原生编辑窗路径。

完整模式默认要求真实编辑/保存/确认由primary物理指针命中实际控件，实际F9按下/松开由primary wl_keyboard送达；断言仅draft变化、底层GTK不变。AT-SPI只读实际可访问名与WINDOW坐标，再结合compositor layer/client实际边框换算，不用DoAction绕过不可见浮层；SCREEN=0,0在Wayland layer上不可信。完整模式等待统一候选，不计通过。

```sh
HV_TEST_CANDIDATE=/absolute/production-candidate \
HV_TEST_HYPRLAND_SOURCE=/tmp/cornice-hyprland-fixed-20261016 \
HV_TEST_CORNICE_PRODUCT=/home/liufeng/.local/share/cornice \
HV_TEST_CORNICE_SOURCE=/home/liufeng/Code/self/cornice \
HV_TEST_EVIDENCE=/tmp/hyprvoice-native-full \
/home/liufeng/Code/self/cornice/test/resource-scope.sh \
/absolute/current-result-voice-edit/tests/revision_focus/run-native.sh
```

仅复现旧baseline路由问题可加HV_NATIVE_ROUTE_ONLY=1（跳过尚不存在的新UI，报告明确route_only=true，不能当完整UI通过）；HV_NATIVE_GEOMETRY_ONLY=1只做坐标/API诊断，同样不算整套通过。

当前已实际读取hyprctl -j seat list：JSON数组中每项name、display、seatId、generation、available、paused，input-target-v2.seatName精确匹配list[].name拿到真实display；主seat不在active-secondary数组，不能选第一项作fallback。原始私有dump在/tmp/hyprvoice-native-mapping/，含测试namespace临时token的原始文件不提交。

### 完整候选仍需通过的路径

普通primary脚本不能证明secondary seat。需要现有兼容Hyprland + Cornice产品只读挂载，cornice-desktopd和原生presentation；实际primary虚拟键盘/指针接管到agent seat，应用通过该seat的Wayland端点启动。不得用强制focuswindow或CLI改target来冒充物理接管路由。

1. agent1 GTK正文录音→浮层编辑/保存→退出接管回primary→重新接管原agent1/原窗口→F9→确认，仅agent1输入一次。
2. agent1待确认结果→agent2或不同窗口→F9/确认拒绝，双方保字；回agent1允许明确复核后的修改/输入。
3. 实际cn.launcher键盘层打开时拒绝F9/输入，不回落隐藏应用；关闭返回原目标后复核。
4. readonly observation、锁定/无控制不允许录音或发送；不会替换选区或进入密码控件。

可以先运行现有Cornice路由探针作为**底层路径回归**，不能替代上述真实App路径：

```sh
CORNICE_TEST_HYPRLAND_SOURCE=/tmp/cornice-hyprland-fixed-20261016 \
CORNICE_TEST_HYPRLAND=/tmp/cornice-hyprland-fixed-20261016/build-release/Hyprland \
CORNICE_TEST_PRODUCT=/home/liufeng/.local/share/cornice \
CORNICE_TEST_VOICE_PROBE=/absolute/candidate/build/seat_input_probe \
CORNICE_TEST_RENDER_NODE=/dev/dri/renderD128 \
/home/liufeng/Code/self/cornice/test/isolated-desktop-test.sh voice-seat-verify.py
```

该旧suite还要求SOURCE/build-agent-session/hyprctl目录；实际当前可用CLI在build-release/hyprctl/hyprctl。若不兼容，应在独立fixture提供精确CLI路径，不修改宿主源码/工具或静默回退日常socket。

## 真X-ASR同样本对照

```sh
/home/liufeng/Code/self/cornice/test/resource-scope.sh \
/usr/bin/python3 /absolute/current-result-voice-edit/tests/revision_focus/replay_tail.py \
--candidate /absolute/production-candidate \
--corpus-root /home/liufeng/Code/self/mobius/apps/hyprvoice/build/corpus \
--model-config /home/liufeng/.config/hyprvoice/config.json \
--baseline /absolute/current-result-voice-edit/tests/revision_focus/evidence/be0f911-tail.jsonl --output /tmp/hyprvoice-candidate-tail
```

只读取配置中的3个本机模型路径，不带hotwords、LLM配置、应用内容；hash校验冻结公开音频，记录模型/binary SHA，实际调用生产replay，禁止下载。与be0f911逐样本字符评分和前缀/尾句比较；否定/数字拒绝策略另由coverage确定性单测检查，不把该公开段落说成覆盖不存在的否定用例。新候选尚未构建时不能称对比通过。
