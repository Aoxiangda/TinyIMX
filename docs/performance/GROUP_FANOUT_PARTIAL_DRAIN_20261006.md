# 部分群投递批次等待修正（2026-10-06）

状态：原生92项PASS，真实ABBA功能正确但性能FAIL；完整原accepted Gateway/Message及Env已恢复。下文最初方案记录保留，当前实测结论见后续段落。

真实对照A2 MID1304：65人64个收件者，49人在201.950879ms前收到，剩余15人从1079.265736ms才到达，ALL1117.585340ms。空档877.314857ms，所有attempt_count1；晚批coordinator领取15条。相同晚收件peer queueage仅0.218ms。源码只有processed>=64才继续领取，部分claim后睡恢复1000ms；SKIP LOCKED下部分批并不能代表全体没有待领取工作。时序与此缺口一致，低频trace没有覆盖每次空claim，不声称完整锁时间线已经证明。

strict defaultOFF开关TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE=1，只影响已启用commitwake的循环。将非空部分批也纳入原每轮最多4批/随后25ms让出；空批和claim失败仍原1000ms等待。保留原SQL领取/租约/提交/completion/ACK/limit64及stop；不增线程、不合并新队列、不跳过completion。源文件只有一个无layout影响inline开关及两个循环条件。

原生待验：固定49+15 mock在不通知、不改变1000ms恢复期限下是否自动继续；连续非满批仍每4批让出25ms；空claim/失败不自旋、Stop及时、OFF及非法值与commitwake关闭保持原行为。测试的mock只验证调度机制，不替代真实SQL、网络和10k–50k全部功能。

原消息、所有失败、原镜像、私有配置和主机应用保留。此修正针对约一秒部分批空档，跨64批completion串行与路由/ACK等成本仍须逐项减少，不能称群全部性能或项目全部功能极致目标完成。

## 原生与真实四轮结果

源码07136bbc、构建助手f4493aa6、控制助手93f6edc9。92原生检查包含先49后15的领取、4批后25ms让出、空批/失败无自旋、及时停止、严格开关OFF/ON/非法01/commitwake OFF及原executor。调度mock不是实际SQL或容量证明。

同Gateway ELF c536f1c3cab1d7a66155bf6fffc029180d022311c78ca0f334278895cb292210，partial OFF/ON/ON/OFF；recipient/defer/trace/commitwake固定开启，Message同30574112 ELF和claim batch固定开启。相同4群/100真实连接，每规模每case33消息含3warm30测量。528消息、23628真实收件及SQL状态3确认、0重复；216公开交互操作148断言均正确，包括好友、私聊、群治理和1.8MiB文件prepare/chunk/finalize/download/resume/字节一致链。

| 轮次 | 群人数 | 发送ACK均值ms | 首收件均值ms | 全收件均值ms | 全收件最大ms | 原100ms样本门槛 |
|---|---:|---:|---:|---:|---:|---|
| A1 off | 2 | 25.965 | 41.370 | 41.370 | 52.113 | PASS |
| A1 off | 16 | 29.710 | 47.157 | 81.197 | 97.411 | PASS |
| A1 off | 65 | 43.381 | 76.562 | 221.350 | 268.409 | FAIL |
| A1 off | 100 | 69.180 | 90.493 | 530.578 | 623.734 | FAIL |
| B1 on | 2 | 25.647 | 41.675 | 41.675 | 46.623 | PASS |
| B1 on | 16 | 29.713 | 47.845 | 80.759 | 115.674 | FAIL |
| B1 on | 65 | 40.607 | 71.938 | 208.863 | 330.305 | FAIL |
| B1 on | 100 | 60.309 | 82.602 | 497.820 | 614.428 | FAIL |
| B2 on | 2 | 24.460 | 39.613 | 39.613 | 47.603 | PASS |
| B2 on | 16 | 27.770 | 45.029 | 77.106 | 95.586 | PASS |
| B2 on | 65 | 41.071 | 74.650 | 212.008 | 325.139 | FAIL |
| B2 on | 100 | 58.521 | 85.956 | 486.567 | 602.091 | FAIL |
| A2 off | 2 | 24.673 | 40.303 | 40.303 | 61.873 | PASS |
| A2 off | 16 | 28.041 | 44.789 | 76.527 | 91.338 | PASS |
| A2 off | 65 | 40.992 | 75.155 | 216.701 | 365.780 | FAIL |
| A2 off | 100 | 57.235 | 84.001 | 489.157 | 590.544 | FAIL |

每单元30测量，不估总体P99；SQL确认轮询是观察上界，不能当实际最后ACK提交时刻。B1的16人115.674ms也FAIL；65/100全部轮次均FAIL，不宣称达到极致。

| 轮次 | 满64批样本 | 领取RPC ms | 派发合计ms | 完成RPC合计ms | 整批ms | 线程CPU ms |
|---|---:|---:|---:|---:|---:|---:|
| A1 | 43 | 32.878 | 70.843 | 307.857 | 412.955 | 53.954 |
| B1 | 36 | 29.832 | 66.410 | 309.901 | 407.573 | 52.483 |
| B2 | 35 | 33.159 | 64.332 | 319.126 | 418.054 | 51.668 |
| A2 | 37 | 32.294 | 62.830 | 317.041 | 413.456 | 50.128 |

完成RPC仍占协调器整批约75%，是本轮明确的主要串行阶段；线程CPU和上述墙钟不能相加，派发包含嵌套路由成本。最大相邻收件间隔四轮约288/283/291/298ms，均在64人分界，含warm；本轮OFF/ON没有重新出现之前877ms空档，不能将样本缺失当作实测消除或因果收益证明。部分drain机制原生正确，但真实平均提升没有稳定ABBA证据；下一步针对原逐条完成RPC及每收件路由，不通过减小持久化/放宽100ms/删除失败/增大批次掩盖问题。

运行结束完整恢复5c2645 Gateway和be8 Message原ELF/全Env，其他16实例、私有配置、持久化1/1/1/0/0、主机其他应用保持。精确新实例身份以.local/codex/group-partial-drain-control-20261006/restore-summary.json为准，旧报告CID是历史记录。源候选没有选入运行；全部功能10k–50k、离线/故障/并发文件/AI容量仍OPEN。原生日志、逐消息/收件、阶段日志、四轮readiness失败和全部完整链均保存在该stage及对应cross-feature stages，原数据不删除。


## 路由与批完成之后重新观察到部分批长尾

新route候选partial0对照A2 MID2895：48+51两partial claim合计99个收件。B早批48，A迟批51在发送1060.946ms才开始，前48全到189.004ms，后51起1091.127ms，ALL1203.057ms，空档902.123217ms；最终状态3和attempt1，无重复。旧partial对照未重现因果增益的结论仍保留。现在在route/completion/claim固定1的同sealed image中，仅partial0/1/1/0，原4批+25msyield/空及失败1000ms/lease/ACK身份/序号/3秒deadline不变；不重新构建或合入尚未验收容量。结果pending，以新group-route-partial-drain-control-20261006全部原始请求和精确restore为准。


## route/completion固定开启后的第二次部分批真实对照（已完成）

控制3ba6007、Gateway同99ee1b4c/a50e500d、Message同f9094e0f/baa2ec54；仅partial OFF/ON/ON/OFF，route/completion/claim/recipient/defer/commitwake固定1。当前image重新通过的164协调器回归含原partial调度四模式测试。528消息/23628真实wire及SQL状态3、216完整功能操作/148断言、0观察重复；原3秒ACK/ALL/SQL确认观察界限保持。全部结果和失败在group-route-partial-drain-control-20261006及cross-feature-groutep1featA1/B1/B2/A2，30测量每单元不估P99或容量。

| case | size | ACK mean ms | ALL mean ms | ALL max ms | 100ms样本门槛 |
|---|---:|---:|---:|---:|---|
| A1 off | 2 | 26.069 | 42.881 | 64.226 | PASS |
| A1 off | 16 | 28.826 | 65.519 | 78.274 | PASS |
| A1 off | 65 | 39.119 | 130.720 | 178.246 | FAIL |
| A1 off | 100 | 68.284 | 327.232 | 415.588 | FAIL |
| B1 on | 2 | 24.840 | 40.375 | 73.545 | PASS |
| B1 on | 16 | 26.967 | 61.418 | 71.989 | PASS |
| B1 on | 65 | 41.426 | 139.113 | 199.507 | FAIL |
| B1 on | 100 | 56.034 | 334.490 | 441.529 | FAIL |
| B2 on | 2 | 24.741 | 41.042 | 46.862 | PASS |
| B2 on | 16 | 29.400 | 66.080 | 91.328 | PASS |
| B2 on | 65 | 38.437 | 136.124 | 163.375 | FAIL |
| B2 on | 100 | 52.616 | 321.398 | 385.705 | FAIL |
| A2 off | 2 | 23.735 | 39.546 | 48.614 | PASS |
| A2 off | 16 | 27.066 | 61.044 | 73.994 | PASS |
| A2 off | 65 | 39.112 | 131.576 | 165.541 | FAIL |
| A2 off | 100 | 56.612 | 315.551 | 374.963 | FAIL |

100人ON334.490/321.398ms，OFF327.232/315.551ms；65人ON139.113/136.124ms，OFF130.720/131.576ms。没有稳定平均收益，不能包装为突破，65/100全部FAIL。两模式都未重新出现MID2895约902ms空档，最大相邻收件间隔（含warm）A1/B1/B2/A2为161.635/207.306/155.484/156.905ms；缺失长尾不证明开关实际因果消除。机制原生正确，但实际全收件仍主要受ACK/peer同步任务排队约束。后续保留boundedpartial机制独立说明，不据此接受万人容量。

原Gateway5c2645、Messagebe8/全部Env/HostConfig/Health/Mounts恢复，其他16实例/配置/持久化1/1/1/0/0/主机应用保持，自有actors关闭，三个候选Message优雅退出0。最新CID以该控制restore-summary为准。下一strictOFF候选核验现有群ACK/peer错误进入4worker控制执行器，而私聊早已使用已有message_executor（原默认8worker）。只移动两处接线到同消息执行器，保持GroupDeliveryOrderingKey、会话epoch/鉴权/durable Get+Confirm/ACK序号/原deadline/有界队列/重试和线程总数；要测私聊、控制功能隔离与同身份FIFO，不能把8worker当作无证据扩线程。
