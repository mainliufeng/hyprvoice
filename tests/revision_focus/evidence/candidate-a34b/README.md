# 固定候选独立验收 · 2026-10-18

固定binary SHA256：`a34bbc4e71953ace94fe142cd73c9c882397445e9fd5826b937df33d38ea20ff`。
副本：`/tmp/hyprvoice-candidate-a34bbc4e7195`。生产worktree尚未提交，来源/dirty状态见snapshot-provenance.json；不能仅用HEAD be0f911冒充新候选来源。后续重编不改变此副本。

- primary真实GUI焦点15项通过：物理点击编辑/文本框、真实键盘草稿、未保存F9/输入门禁、保存后F9、撤销、错误窗口拦截、返回原窗再次语音修改、确认输入一次。
- Cornice native完整29项通过：真实primary键盘F9按下/松开、物理指针编辑/完成/确认、实际cn.launcher、不同seat/窗口拦截、agent1往返后修改/撤销/准确输入、agent1关闭编辑后agent2重新创建真实GdkDisplay编辑/保存/F9/撤销/输入。原目标、其他窗口、primary正文隔离。
- GUI两套均使用真实生产App、GTK、PipeWire私有零源、合成ASR/回环HTTP；不代表真实麦克风或外部模型语义质量。
- 同SHA真实本机X-ASR回放冻结公开long-en_us-dev，音频及模型hash一致，结果与be0f911逐字相同，末句完整、前缀一致、字符退化门禁通过。warning为空，此样本本次未触发尾句修复分支；否定/数字保护另由确定性coverage测试证明，不能泛化为所有自然语言质量提升。
- 子进程全部停止，native viewer心跳错误为空。资源CPU200%/3G/512进程/20min，PID/网络/HOME/D-Bus/PW隔离、render-only GPU，无日常窗口/音源/外部模型。

上一固定binary 4d1a的真实P1：保存返回原窗后，历史focus_changed_让F9新录音仅0.128s便自动结束。实施线程确认修复，a34b通过；没有放宽recording断言。保留previous-4d1a-confirmed-failure.json。

夹具问题单独处理：GTK layer的AT-SPI SCREEN坐标为0,0，需WINDOW坐标+compositor实际框；GrabFocus返回值不代表真实键盘落点，改用物理点击；native presentation必须续5秒viewer租约；native键盘用同一primary输入设备逐字发送。没有用DoAction或edit-result绕开真实secondary编辑。

本线程未修改生产/Cornice/宿主，未执行main合并、push或安装。最终部署二进制若再变化，需按实际变更复验，不能把本SHA测试直接挂到另一份二进制上。
