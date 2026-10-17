# Release 0.3.0 第一份固定候选（等待局部UI修复后的新SHA）

源码e759cce59a0749a3346e6ccb6522cea19c063c71，工作树干净，CMAKE_BUILD_TYPE=Release。
生产binary SHA256=2e1465373f0fd60f604b790d55ba8e71254b123d2eef8083c3eada9da37011dd。
仅测试生产binary，未把test-only recovery_app冒充产品。

primary15项/native29项通过，真实键盘指针输入、draft隔离、F9按下/松开、undo、launcher与错误seat/窗口、agent1→agent2编辑display生命周期均覆盖。所有子进程退出，viewer租约无错误。GUI音频零源、ASR/HTTP合成替身；另有真实本机X-ASR公开尾句同模型回放，结果与be0f911逐字、前缀一致，末句完整且评分门禁通过，warning空，未触发尾句修复分支。

实施线程完成冻结后又发现候选scroller150px会截断下方来源，拟增到220px。此处结果只对应2e146，不是下一SHA的验收，不可用来部署未验证的后续二进制。具体冻结来源见snapshot-provenance，图像均为真实私有App窗口，无现场用户内容。
