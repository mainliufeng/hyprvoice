# 最终 Release 0.3.0 独立验收 · 2026-10-18

生产提交3730ecbc9ed424d14f1e7dbf4d3f60e7b965f984，Release生产二进制SHA256：
`37709bdf4b6cad1a3cd047c1e272f24be676ebfda1949dbc22b026abbfe82612`。
冻结副本`/tmp/hyprvoice-release-37709bdf4b6c`。此处只验证生产hyprvoice，未使用test-only recovery_app。

快照source HEAD即3730ecb，生产树已核对一致；拍摄快照时只有tests/capture_interruption_cases.py未提交测试更新与docs/result-review未跟踪，详见snapshot-provenance.json。不能宣称当时整个工作树干净。

- primary真实GUI 15项全部通过：物理点击编辑、真实键盘草稿、未保存草稿的F9/输入拦截、保存后按当前正文F9、撤销、错误窗口拒绝、返回原窗多轮修改、明确确认仅输入原控件一次。
- Cornice native真实GUI 29项全部通过：primary wl_keyboard真实F9按下/松开、原生agent1编辑与往返、第二个agent2 GdkDisplay编辑生命周期、实际cn.launcher、不同seat/同seat错误窗口/primary拦截、回原目标修改/撤销/确认。完整模式route_only=false、geometry_only=false；不是CLI写草稿或AT-SPI DoAction代替真实编辑。
- 两套均运行真实生产App、GTK与私有PipeWire零源，ASR与回环HTTP为受控替身；不证明真实麦克风识别、外部文字模型语义质量。Fun单假设夹具不覆盖双ASR来源失效等定向状态机用例，此类受控后端测试由实施线程单独验收。
- 同SHA真实本机X-ASR回放冻结公开42.4秒long-en_us-dev，模型与音频hash均记录于tail-report.json。与be0f911基线文本/前缀逐字相同、末句完整、字符退化门禁通过。warning为空，本次没有触发尾句修复分支；中间句遗漏仍在，不能据此宣称普遍识别改善。
- 所有测试子进程已停止，native租约错误为空。既有scope限定CPU200%、内存3G、512任务、20分钟，私有PID/网络/HOME/D-Bus/PipeWire、render-only GPU，不接日常桌面/物理麦克风/外部模型。

已目视真实primary草稿、native agent2编辑窗和返回目标后的待确认结果：编辑内容与有效正文分开，按钮可达、无正文遮挡，错误目标警告可见。截图中的英文与目标编辑器均为合成测试内容。可从focus-01→02→03→04→05及native序列复核真实流程。

release-2e14、release-3372、release-c6b4与candidate-a34b均为旧版本证据，不与本次SHA混用。本线程只提交独立测试与证据；没有合main、push、安装或重启服务，最终交协调线程审查后由实施线程发布。
