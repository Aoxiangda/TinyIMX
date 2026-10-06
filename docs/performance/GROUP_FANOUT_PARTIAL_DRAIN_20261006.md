# 部分群投递批次等待修正（2026-10-06）

状态：SOURCE_ONLY，尚未构建和实际性能验收；原accepted Gateway/Message运行保持。

真实对照A2 MID1304：65人64个收件者，49人在201.950879ms前收到，剩余15人从1079.265736ms才到达，ALL1117.585340ms。空档877.314857ms，所有attempt_count1；晚批coordinator领取15条。相同晚收件peer queueage仅0.218ms。源码只有processed>=64才继续领取，部分claim后睡恢复1000ms；SKIP LOCKED下部分批并不能代表全体没有待领取工作。时序与此缺口一致，低频trace没有覆盖每次空claim，不声称完整锁时间线已经证明。

strict defaultOFF开关TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE=1，只影响已启用commitwake的循环。将非空部分批也纳入原每轮最多4批/随后25ms让出；空批和claim失败仍原1000ms等待。保留原SQL领取/租约/提交/completion/ACK/limit64及stop；不增线程、不合并新队列、不跳过completion。源文件只有一个无layout影响inline开关及两个循环条件。

原生待验：固定49+15 mock在不通知、不改变1000ms恢复期限下是否自动继续；连续非满批仍每4批让出25ms；空claim/失败不自旋、Stop及时、OFF及非法值与commitwake关闭保持原行为。测试的mock只验证调度机制，不替代真实SQL、网络和10k–50k全部功能。

原消息、所有失败、原镜像、私有配置和主机应用保留。此修正针对约一秒部分批空档，跨64批completion串行与路由/ACK等成本仍须逐项减少，不能称群全部性能或项目全部功能极致目标完成。
