# 先分析、再修正、最后验证：性能根因复盘（2026-10-05）

当前运行：MCP修正已部署bc85/编译778，其他18保持；主性能目标未达，隔离RPC诊断待测。以下历史阶段原文保留。

最新完成结论如下，之后的“待执行”文字为保留的历史阶段记录。

### 2026-10-05：提交合并候选否决，转向消息增量成本分析

实际执行的 gc150u1000 保留完整 10k 认证、9000 计划/发送/正 ACK/wire/SQL 接收确认；心跳 82382/82382，零负 ACK、skip、late、断连。P99 227.5ms、计划到 ACK 230.0ms、最大 511.192ms，仍然 FAIL；原版 io150base2 为 188.0/190.7ms。主 finally 已恢复 delay=0，独立看护在验证后正常取消；19 个容器的 ID/镜像/启动时间、全部私有配置 SHA、持久参数均未变化，SQL 保持 1/1/1/0/0。没有保留 1000us 参数，也不继续尝试盲目延时矩阵。

有效计数窗口 58.3976s：binlog file MISC 6069/103.925 次每秒（原版 12317/210.908），但精确 redo fsync 12844/219.941 次每秒（原版 13018/222.912）基本没有减少。COMMIT 均值 11.3823ms（原版 7.5416），确认 UPDATE 13.6419ms（原版 9.3398）。guest 忙碌 85.570%，system+softirq 39.833%，CPU PSI stall 64.362%，全 guest fork/thread 320.030 次每秒；提交和消息延迟都没有显示收益。MISC 不是纯 fsync，SQL 均值不是总体 P99，顺序运行且保留其他应用，不能把所有差异因果归给该参数。

两轮各 12 个 Docker CPU 帧均显示 MySQL（约 1.46/1.48 核）、Message（0.89/0.93）、两 Gateway（各约 0.75）、Redis（0.61/0.62）是当前主要服务成本；outbox/unread 投影器各不足 0.01 核。Docker 帧是滚动样本，不能与 /proc 连续积分相减后把差额归给独占内核成本。

冻结 auth10kdiag 的纯在线窗口，排除前 5s，仅纳入完整包围的四段共 20.0467s：guest 忙碌 31.404%，system+softirq 13.743%，CPU PSI 11.668%，context switch 19707.79/s、全 guest fork/thread 51.180/s。Gateway 精确 cgroup CPU 各 0.259/0.260 核，User 0.032；相比消息窗口 85% 忙碌和 315–320/s 创建量，消息链路增加了大量工作。此对照跨时间且 User 诊断镜像不同（选中阶段无登录），不能报告严格因果百分比或把所有新线程归给 Message。

另对已有 sw150a/mp150diag 的七服务 cgroup CPU 做有效分项：Message 原版 0.969 核中用户态 0.463、内核态 0.506；被拒绝 poller16 为 0.913 中 0.454/0.460。MySQL 原版 1.403 中 0.861/0.542；Gateway 各约 0.71 中内核态各约 0.33。没有观测这些 cgroup 自身限流，cpu.max 均 max；这不排除 VMware 或共享主机等待。init PID 的线程数不能当业务线程数，cgroup 包含所有子进程故 CPU 分项仍有效。大比例内核时间支持继续定位 RPC/网络往返/调度，但尚没有 syscall/flamegraph 级的唯一根因证明。

所有候选失败、参数、SQL 计数、分析、看护、精确回滚和源码工具保存；v15 本机已验证 57 文件，SHA ece33159841d81f10a1cf9060cda9d4cf0c99e86e4bfe134bc8bb5727e34f372。v16 将保存完成的失败候选及这次只读复核，不覆盖 v14/v15。后续应测量实际消息成本再改代码，避免重复扩池/扩线程/改提交延时。当前全部功能 10k–50k 的极致性能目标尚未实现。


最新存储窗口补充：原User恢复后，io150base2完整10k认证、9000计划/发送/正ACK/wire/SQL确认全部一致，HB82296完整、无skip/late/断连，P99188.0ms、scheduled190.7ms/max438.252ms，严格延迟FAIL保留。成功取得58.3998s有效窗口file/status/digest/procstat/pressure增量；两次快照耗865.949/1138.598ms，数字有观察开销和非同步读取边界，不是未干扰的性能接受。确认UPDATE8827次均9.3398ms、COMMIT9401次均7.5416ms，精确redo fsync13018/约222.912每秒，binlogfileMISC12317/约210.908每秒、均1.8746ms（MISC不是纯fsync，累计MAX不是窗口P99）。guest CPU busy85.297%，其中system30.290%、softirq9.700%，context-switch约38061/s、全guestfork/thread约315.241/s；CPU PSI增量63.328%，IOsome2.084%、memorysome0.0041%。提交同步成本与调度压力都存在，不能称fsync或RPC线程为唯一根因。源码/运行参数未改变。

v14归档SHAefe242c22d61b3dc90f333444a114bbf7047afa26a5d4f3bfe016d63ae00f4b1，本机109文件逐个SHA验证；认证主机窗口另存27点CPU均50.56%/max71%、vmware单核100口径均591.30%，CIMmax704.851ms。原PowerShell JSON日期自动转型后再次字符串解析导致0匹配，原无效review保留，Python按原始有时区ISO字符串纠正，不能把这些成功窗口数据归给较早的登录abort。

下一候选先保存Git/审计，只暂时SET GLOBAL binlog_group_commit_sync_delay=1000微秒，保持flush=1/sync_binlog=1/binlogON/no_delay_count0。MySQL官方8.0说明该设置可能减少同步调用，也可能增加延迟/竞争；因此需要同一原版10k150/s窗口验证文件/提交/确认成本与真实ACK分布，而不是假定改善。候选finally精确恢复0，清理异常仍恢复；处理INT/TERM/HUP，仅停止本任务创建的客户端组，独立450s看护只在同MySQLID且状态仍为本候选时恢复，拒绝未知外部更改。无SET PERSIST/config/重启/主机或VM变更；全19容器和配置/索引保持。此阶段候选尚未执行，v15保存完整原版I/O及待执行源码工具；所有功能10k-50k极致性能未实现。

最新完成补充：374f280认证诊断通过36检查（计时16、User应用6、真实gRPC关7/开7），User-only1ff078镜像仅增加一个诊断开关，其他18/config保持。auth10kdiag在原100用户/s爬坡和3s期限下10000全部登录，hold30s/61440心跳全部响应，登录完整总体P9986.5ms、均35.3858ms，无断连。402同uid/TID/时间包含的成对样本：仓储wall20.3534ms/CPU13.7974ms，查库2.1142/0.4974ms，密码18.0865/13.2046ms；handlerwall20.6654/CPU13.9881，额外wall0.312/CPU0.191ms。采样为uid%16且日志限速，以上不能当总体P99或与全客户端均值相减测transport。本轮没有复现原秒级认证失败，也没有证实诊断代码改善性能；原失败保持未解释的瞬时排队问题，未删改或放宽门槛。

诊断User已恢复原38dca镜像和ae1b6f二进制，原环境键和值完全一致、诊断开关移除，其他18/所有配置保持。回滚脚本按ENV数组顺序比较而误报AssertionError，独立只读post-review确认仅排列不同，没有再次重启。资源31点cgroupCPU有效，20个完整爬坡间隔User均1.5932核、GWs各0.4902/0.4899核，guest平均busy82.5%、memory最低8494776KiB，未观测服务自身throttle；maxcapture17.431ms。原线程计数来自Docker initPID，仅适用于init，不能作为业务线程数；另存复核确认当前实际child31/57/57线程（这是回滚后快照，不代表此前诊断窗口）。实际认证SYS_gettid402样本326不同TID可用，但不据此盲目再次加poller。完整直方图桶已向上取整，旧复核多加0.1ms；保留旧复核并另存准确值：sw92.1、mp89.7、io失败子集2627.5ms。v14待导出，以上所有阶段/失败/工具源版本均保存。下一步恢复原运行版本后采集尚未成功取得的存储窗口增量，groupcommit保持0。

最新状态补充：原版本 I/O 基线 io150base 在登录阶段中止，未进入有效负载窗口，不能计算文件/fsync/digest 增量，也未改变 groupcommit 设置。1222 连接、1023 成功认证、一个真实 business_deadline_exceeded；成功子集登录均886.673ms、P99上界2627.6ms，而前两轮完整10k均约33–34ms、P99约90–92ms。原始失败、observer-error 和只读复核均保留。实际33fc编译Gateway在 ExecuteTask 开始前判断原3s期限，过期后派发该负响应，证明至少一条任务排队过期；不能据此断定密码计算或主机应用是唯一原因。源码数据库连接在 FindByUsername 返回时释放，Redis Ping已在池锁外，均不是新修复。

下一阶段只增加默认关闭的认证数字分段诊断：查库、PBKDF2和UserService方法的wall/threadCPU，保留KDF100000及所有鉴权/状态/期限语义。User handler计时不含gRPC入口前等待或返回后transport，wall-minus-CPU仍不能单独判I/O或调度。成功uid%16为非随机样本，repo+handler共用8/秒bucket日志上限，只有同uid/TID/包含时间区间的样本才可配对。此次源码/测试/审计与失败证据先保存Git，构建/部署/测量此时未运行；运行Gateway1d8、Messageb24、User38dca及SQL设置保持。以下“最新迭代”是先前阶段历史记录，不代表I/O基线尚未尝试。

最新迭代更新：f7 MAX_POLLERS16通过205检查但同诊断150/sP99225.5ms，线程仍107/113不同TID，未证实性能收益；已回退原Messageb24/environment并恢复默认2源码，失败Git和v13的74文件保留。前轮a315诊断278检查及P99179.0FAIL的v12共88文件也保留。池mutex/slot低耗时不支持盲目扩池；仓储CPU远短于wall不能独自判IO或调度。下一阶段先采集原版本同窗口提交/日志同步/file/status/digest和guestCPU增量，再决定是否测保持双1持久性的groupcommit合并。当前这一步尚未运行，所有功能10k-50k极致性能仍未实现；以下为历史阶段分析。

## 当前结论与操作边界

收到“先不着急继续测，遇到问题先分析”的要求后，停止新增压测、参数实验、构建和部署。本报告只分析此前已经完成的测试、已有日志和源码。最新四轮负载在这项要求前已结束；没有继续发起同样的负载。

**已定位的失败机制是：同步消息链路占用有限执行容量，负载上升后网关积压，排队消耗请求预算，随后发生拒绝、期限错误和确认滞后。** 这说明应该修正每次业务的实际成本及发送/确认之间的竞争。尚不能断言数据库连接池、fsync、CPU调度或某个RPC是唯一根因；现有记录没有把这些组成部分完全分开。秒级排队是直接测得的现象，不是对下游归因的充分证据。

运行版本保持 Gateway1d8/16、Messageb24/pool16、单索引013。Gateway编译Git为33fc9bb，Message为ddc7e8e；分析前仓库HEAD为6521fe6。公平交还候选的源码保存在Git，但其运行镜像已被拒绝并回退。仓库HEAD、候选源码和实际编译版本必须分别报告。

只读核对当前两个Gateway真实启动参数所指向的挂载配置，确认每个消息执行器为 **16工作线程、512待处理任务上限、64分片、每分片容量64、期限3000ms**；foreground配置工作线程4。不能用头文件默认128/32代替实际512/64。pending计数覆盖部分执行中任务及尚未完成的网络completion，不能把512直接描述为“512个纯队列等待请求”。配置SHA分别为b85d603b05c35026ae605e242ba19b98050be40a986ac1d6c5a1f62af3c738a9、68801ea0c85e088bbe4b9e40169ed4106f7b8b7fe2f9d9570754a422724adaba。

## 1. 已有数据把性能拐点定位在哪里

固定10k认证在线、Gateway1d8/16、Messageb24/pool16/013、私聊128字节、计划速率和60秒窗口；保留其他应用，不清空业务历史。150/200/250为回退后的新控制，100/300为同版本此前控制，属于不同运行，不能据此报告严格单次实验因果百分比。

|运行|计划私聊/s|发送|正ACK|负ACK|正ACK P99|计划到ACK P99|窗口正ACK/s|
|---|---:|---:|---:|---:|---:|---:|---:|
|acount10k1|100|6000|6000|0|75.8ms|77.7ms|99.95|
|base10k150a|150|9000|9000|0|168.2ms|170.2ms|149.9|
|base10k200a|200|12000|11177|823|2929.5ms|2931.0ms|182.817|
|base10k250a|250|15000|10424|4576|3000.8ms|3003.0ms|168.667|
|acrate300a|300|18000|10408|7592|3006.7ms|3007.9ms|167.967|

各轮没有跳过计划请求；250和300各有1个迟发，其余这些运行迟发0。该表P99仅覆盖正ACK，负ACK必须同时报告。150/s的60秒零负ACK结果是短窗口观测点，不是长期稳定容量，且已不满足100ms最低线。200/s起已经出现明显业务失败，不能用“正ACK吞吐约180/s”当零错误容量。

只读提取相应时间窗内、用户范围匹配的数字阶段日志：

|运行|网关派发等待采样中位数|网关实际执行采样中位数|持久化RPC采样中位数|仓储持久化采样中位数|
|---|---:|---:|---:|---:|
|base10k150a|1.525ms（318）|114.296ms（318）|82.867ms（318）|15.907ms（488）|
|base10k200a|652.048ms（703）|97.025ms（703）|60.539ms（668）|24.851ms（500）|
|base10k250a|1570.578ms（725）|96.291ms（725）|66.801ms（634）|27.562ms（510）|
|acrate300a|1644.245ms（729）|94.029ms（729）|69.057ms（607）|25.461ms（504）|

括号为样本数量。这些是限速慢样本，时间范围包含爬坡/收尾，且各列总体不同；不能相减列中位数、相加成总体P99，不能把150/s执行样本更慢解释为200/s更快。它们支持两个不同问题：150/s尚未出现普遍秒级积压，但单次业务链路已存在百毫秒慢请求；200/s以上，队列等待成为直接测得的主要延迟段。既要降正常路径成本，也要防止容量耗尽后的排队放大。

## 2. 超时并不全是“关系服务慢”，确认滞后不是测试通过

完整负ACK文件计数与各轮总负ACK一致：

|运行|overloaded|relation_service_deadline_expired|message_persistence_deadline_expired|message_persistence_uncertain|
|---|---:|---:|---:|---:|
|base10k200a|29|731|14|49|
|base10k250a|1674|2663|55|184|
|acrate300a|4312|3043|52|185|

`SocialRpcClient`在剩余预算已经耗尽时可以在发RPC前本地返回DeadlineExceeded，所以关系期限错误不能全部归给SocialService的SQL。300/s关系查询SQL摘要均值约1.479ms，包含爬坡/收尾，既不代表RPC总体延迟，也不支持把关系SQL视为秒级延迟唯一来源。

`message_persistence_uncertain`表示越过写入边界后结果不确定，不能直接重新生成业务消息身份或认定未入库。原300/s有164个已经持久化却没有正ACK的逻辑消息。原500/s在验收快照中Pending545、正ACK综合对账异常529；稍后只读复核全部11580行身份未变、Pending归零、原529均SQL确认。这是确认晚到的证据，**原始FAIL保持**，不补写“期限/wire通过”，也不做SQL修复来改结果。

## 3. 源码已确定的成本与竞争点

1. **同步调用占用消息工作线程。** 真实入口为`examples/gateway_demo.cpp`，私聊发送、peer转发、receiver ACK使用MessageExecutor。发送路径有权限RPC、路由、持久化RPC和幂等未读投影；当前阶段之间有串行依赖。线程在等待RPC时仍被占用，队列容量增长只能容纳更多未完成工作，不能降低每次处理成本。
2. **确认回执与新发送竞争同一执行器。** `GatewayServer::HandleReceiverChatDeliveryAck`向MessageExecutor提交MustRun任务，以稳定message_id排序；确认RPC同步完成。每次成功发消息还引入接收投递/确认工作，实际任务到达量不等于私聊发送速率。仅300/s旧控制就观察到18996次wire投递和同数receiver_ack_queued；这含重投/历史工作，不能把它们当18996个唯一新消息，但确实存在额外处理量。
3. **单条接收确认发生两次连接获取/检查。** `MessageApplicationService::ConfirmReceiver`先`GetPrivateMessage`读取完整记录，校验收件人和状态，Pending时再`ConfirmReceiver`更新。底层先`FindPrivateMessageById`获取/释放lease，然后`MarkReceiverConfirmedBatch({M})`再次获取lease。`MySqlConnectionPool::Acquire`每次检查Ping；原实现在Ping前已解锁，不能错误描述成持池锁做网络Ping。两次租借路径是已确认的源码事实，但节省它对总体P99的贡献尚未实测。
4. **RPC与仓储内部时间有未解释部分。** 在同消息ID匹配的原300/s106组采样中，持久化RPC减仓储内部时间均值30.411ms；新200/s122组均值28.220ms。这里只扣除同一请求的内部阶段，仍混合RPC服务入口等待、客户端/服务端调度、网络和编解码，不能直接叫作纯网络延迟，也不是整体P99。
5. **数据库提交和确认写入不是免费操作。** 原300/s慢样本COMMIT均值12.839ms；窗口SQL摘要接收确认UPDATE10582次、均值14.176ms、累计语句耗时150013ms。累计跨并发连接，且包含窗口外段落，不等于独占150秒CPU或磁盘，也不能当作fsync耗时；说明确认路径值得减少冗余开销，但还缺确认RPC/池等待的独立阶段记录。

## 4. 两个已失败候选缩小了什么范围

|候选|300/s正ACK/负ACK|正ACK P99|派发等待慢样本中位数|结果|
|---|---:|---:|---:|---|
|原16线程|10408/7592|3006.7ms|1644.245ms|FAIL|
|同镜像32线程|11185/6815|3008.2ms|1651.452ms|FAIL，已回退|
|16线程分片公平交还|10635/7365|3016.1ms|该轮未提取，不能填0|FAIL，已回退|

32线程慢样本持久化RPC均值75.215→136.777ms，Acquire均值6.340→22.708ms。线程加倍没有消除积压，且下游等待增长；这是反对继续单纯增加线程的证据，不是证明“池16就是唯一瓶颈”。Acquire混合池锁/槽位等待、Ping和重连，尚未拆分。公平交还184项正确性检查PASS，真实性能仍不满足要求，因此不能把“单个stripe连续执行”当作已经找到的唯一原因，候选不接受。

## 5. 下一步修正顺序与停止条件

**先做明确的代码成本修正，不先重跑同一矩阵。** 每个候选必须先审计路径、原字节/SHA、语义影响和回滚，再提交独立Git版本。下述尚未实现或验证，不能写作已经完成的性能收益。

1. **把单条收件确认改为一个lease内完成校验和有条件更新。** 新的仓储/application接口必须保留完整记录合法性检查、NotFound、错误收件人拒绝、Failed状态拒绝、重复确认幂等、Read不降级、并发确认/已读的单调状态。Gateway继续校验会话、M/receiver/delivery_seq；不能提前声明持久确认，也不能只凭客户端message_id更新。沿用原条件更新，避免额外长事务/全局锁。先补与实际状态转换有关的回归和故障测试，再考虑性能验证。
2. **设计发送与确认容量隔离，同时约束下游总并发。** 为确认保留独立、有界的处理能力，避免新发送积压抢光确认机会；不能简单新增无限队列或把两类并发叠加压向同一个pool16。确认必须允许合法重复/恢复，保留MustRun责任与关闭收尾。方案应明确重试、背压、总并发和pool占用后再改代码。
3. **按需要补足阶段证据，选择RPC/池的下一项修正。** 区分池锁与槽位等待、Ping、重连；记录真实确认RPC执行及排队、消息入口排队、线程CPU与wall、事务提交/IO等待；用稳定消息身份和统一时间边界关联。已有MySQL分段计时仅是本地未应用草稿，没有部署，也没有任何真实分段结果。若未来采用，必须重新审计，因为当前文档提交会改变原三路径草稿的基线；禁止拿旧审计包覆盖新文件。需要新增运行证据时，先说明究竟要判定哪个假设，再安排最小验证。
4. **若同步等待确认为主要线程成本，再实现有界异步RPC推进。** 不在普通消息线程中持续阻塞等待；保留每key顺序、权限依赖、稳定消息身份、message/outbox原子持久化、模糊提交恢复、投递/ACK边界及退出收尾。不能并发化有正确性依赖的阶段，也不能以提前ACK/弱化持久性换取低P99。

重新进行性能验证的前提是：具体问题、对应修正及正确性/故障回归均已明确并通过。届时只做与该修正有关的固定版本对照；如果无改善，保留失败、回退候选并重看分段证据，不继续以同一代码反复加压。任何更高规模或长期混合矩阵需等这一轮成本/排队问题得到解释和修正之后。

## 6. 仍不能忽略的其他边界

- `base1k300a`计划18000、实际发送16755、跳过1245，单连接只允许一个未完成私聊请求；它不是完整300/s开环对照。该轮已发送部分仍4664负ACK、P992990ms，只能作为“单靠降低在线数不能证明问题消失”的辅助证据，不能据此精确拆分在线维护和服务器处理成本。
- 10k/20k/30k历史100/s控制同时出现CPU等待PSI上升和约8GiB可用内存，部分轮次内存PSI为0。CPU等待不是CPU利用率，已有数据不支持把缺内存当主要解释。游戏/其他应用保留，不清空全局缓存、交换分区或业务历史。
- 50k认证未达到完整全在线稳态；密码PBKDF2、认证排队/CPU需要独立分段，现有perf预检未通过，没有火焰图证明密码函数占比。不降低密码成本、不延长期限制造通过。
- MCP page100与MessageRpcClient最大50的接口约束不一致仍未修复；Ollama实际guest loopback/qwen2.5:7b与原AI配置不匹配，正式principal也有问题。49项低样本链通过不等于所有功能高并发P99通过；登录、群聊fanout、离线恢复、文件、MCP/AI、TLS、故障及长稳态还需分别验收。
- “极致”必须以当前资源、业务到达强度、功能比例、成功吞吐、完整错误率、P50/P95/P99/P99.9及一致性来描述。100ms仍只是最低线；现状总体FAIL，不能写成“10k–50k全功能低延迟通过”。

## 7. 保存与可复核性

已完成记录分版本保存：v7为771文件SHA通过，v8为235文件SHA通过，v9新增四轮原始worker/失败响应/ledger/SQL对账、运行前后身份及本次只读分析，共155文件SHA通过。v9归档SHA为`4affee9157479fc7e71328237ddc283b5b68f001d3547656865913d11c80d798`。两个只读诊断脚本预检失败也保留：首次INFO启动日志不足，第二次把配置目录挂载当单文件；第三次按真实Cmd配置参数映射挂载路径完成。没有隐藏失败或修改原始测试结论。

本机位置为`evidence/post-restart-v9-20261005/restored-curve-causal-review-20261005-attempt3/summary.json`及四个`capacity-base*`目录；guest原件位于`.local/codex/`同名目录。所有运行私有配置/凭据/原始私有日志留在guest，导出仅允许数字/脱敏结果，凭据精确扫描通过。历史负载脚本保存为证据，不作为当前继续运行的指令。本次只提交文档和历史证据工具，不构建、不部署、不清理、不新增压测。


### 2026-10-05：修正 MCP 消息分页工具契约（验证待完成）

先前真实隔离 MCP 的 page100 会话查询失败有确定代码原因：MCP schema/校验接受 1–100，而 MessageRpcClient 与 MessageApplicationService 的会话/历史读分页最多 50。仅这两项工具改为 schema 和本地校验 1–50；缺省 50、合法 1/50 原样传递，不悄悄截断；51/100 等已经不能完成的请求现在在发起 RPC 前返回 invalid_arguments。好友、我的群、成员列表保留 100，鉴权/身份/查询/期限与 SQL 完全不变。

新增回归检查两类工具边界、缺省、无效类型与大整数，并验证无效参数不调用后端、其他三类工具仍允许 100。构建先在独立目录将原 domain 源码与新测试链接，保存应有失败；再只构建 MCP domain/core/server，使用缓存依赖并单线程，保存原二进制 SHA 与 red/green 日志。构建不会部署或改变 19 个服务，尤其不构建当前仍保存的已拒绝 Gateway 公平调度源码。之后真实隔离 MCP 验证需独立审计，当前没有性能接受结论。

v16 脱敏归档本机 70 文件逐 SHA 通过，archive SHA 488f8ceb9a79614ac0848aa370ddf9abcae8b9c0c19e855572794bfa2b6bbcbc，保留提交合并失败、精确回滚及 CPU 对照。MCP 契约修正不等于私聊尾延迟改善；10k–50k 全功能极致性能、AI 服务/正式 principal 和每功能混合负载验证仍未完成。


### 2026-10-05：MCP 分页修正原版失败/候选通过，真实验证待执行

源码 Git 778ad55708b1806d09c7691b245f870b17861983。将精确原版 domain 源（SHA 1a5ef6126f113c057fc18c2538febb896cff6b96c7b08576467e808ba9b538f5）与新增边界测试在独立目录编译链接，原版明确失败于 schema matches each domain page contract，exit1；不是编译或运行错误。修正后 domain 89 项 PASS，core exit0，core 原测试没有逐项 PASS 输出，不能杜撰检查数量。仅 MCP 单线程缓存构建完成，候选 server SHA dbfef7cc761bc3253d4269038c63e1c347974c73574b5df1e07055fed201d926；原 MCP/core/domain 二进制已逐 SHA 备份，全部 19 实例/配置保持，未部署。

本机生成候选时曾用 Windows 默认 GBK 读取中文文档而失败；部分本机 C++/测试候选保存于 evidence/mcp-page-preparation-encoding-failure-20261005，规范审计后只恢复两项本机 preimage，生成器明确 UTF-8 后成功。该失败发生在打包/上传前，Ubuntu/运行时未改变。生成和恢复工具随本次 Git 保存；保留失败，不掩盖。

待执行真实验证使用已拥有的 principal519870/peer519872/group26/file16，候选原生进程只监听 127.0.0.1:18322，随机 token/私有配置不出目录。八类真实只读 RPC、消息 1/50/缺省、12 非法 limit、未授权/身份注入与其他三类 page100 均验证。strace 由父进程跟踪自己的子进程，-c 只保存 syscall 聚合，绝不附着正式服务或输出参数/凭据；计时受干扰，不能用于容量接受。15s 启动/5s 请求/150s 整体与自己的看护有界，INT/TERM/HUP finally 仅在 PID/cmdline/starttime 全部吻合时停止自己的候选；正式 19 服务/SQL1/1/1/0/0/所有应用保持，不做新 fixture 写入、模型推理、安装或删除。该真实验证此阶段尚未运行。

主性能排查已核实 Message 通道按 target 缓存复用、ZooKeeper Resolve 读本地快照、TCP_NODELAY 已开、遥测为批处理导出。这些机制不支持重复修复“逐消息建连接/实时 ZK 查询/Nagle/同步逐条遥测”的猜测。尚需更细的实际 CPU/RPC 往返证据后实施主要性能修改；全部功能极致性能仍未达到。


### 2026-10-05：MCP 真实边界验证通过；当前主机环境已只读核实

独立回环候选 compiled778/dbfef7 完成 27 请求：八类真实领域 RPC、消息 1/50/缺省六个合法用例、两消息工具 51/100/0/-1/字符串/null 共十二个非法用例、未授权和身份注入检查。好友/群列表/群成员 100 保持有效；已有测试身份519870/peer519872/group26/file16均正确、文件字节校验元数据未变。仅父 strace 跟踪自有子进程，汇总 syscall 数量不含调用参数；futex/epoll 总耗时包含等待且受 ptrace 干扰，不能当独占内核 CPU 或生产 P99。候选按 PID/cmdline/starttime 验证后已停止，全部19/配置/SQL1/1/1/0/0保持，没有模型推理或业务写入。

下一步封存原38dca镜像+仅 MCP 二进制，验证UID1000加载和缺配置预期exit1；之后仅 MCP 更新，环境键值/命令/健康检查/正式 principal/模型/配置原样，其他18保持。无活跃MCP请求和压测时执行，验证现有token的工具声明50/100，不导出token。任何失败精确回滚38dca/8cd229，并验证ENV映射而不是数组排列。当前 image/deployment尚未执行；这项功能修正不表示正式principal1缺失或AI连接已解决，也不表示10k–50k性能已接受。

Windows21:39UTC只读审计：实际运行VMware16.2.4 build20089737、Ubuntu VMX8vCPU/16384MiB，当前vmware.log Monitor Mode=ULM且WHP标记存在，Windows HypervisorPresent=true、VirtualizationBasedSecurityStatus=2。真实VMX SHA726444586dbb79772b4a2127581f11fcd75d9a5087a0c9b033f6a5aed705ad4f。SecurityServicesRunning=[0]，不据此声称HVCI/内存完整性功能正在运行。VMware官方说明ULM使用WHP API： https://blogs.vmware.com/cloud-foundation/2020/05/28/vmware-workstation-now-supports-hyper-v-mode/ ；Microsoft说明状态2表示VBS运行： https://learn.microsoft.com/en-us/windows/security/hardware-security/enable-virtualization-based-protection-of-code-integrity 。此为当前环境因素，没有与CPL0成对比较，不能分配延迟因果百分比或否定软件瓶颈。没有改VBS/启动项/VMX/系统设置、重启或停止任何应用。

虚拟网卡确为e1000，但guest ip route get192.168.220.128 from192.168.220.129结果local/devlo；本地压测及Docker服务链并不以物理虚拟网卡为主要传输路径。因此不盲改网卡。当前有依据的方向是进一步区分原消息组件的SQL同步/网络往返/RPC与线程成本，再选择代码优化；不是把所有高内核时间都归给VMware。主机审计JSON在本机evidence/vmware-host-mode-review-20261005，只有允许的模式/硬件字段，没有完整VMX/log或命令行。

v17将保留MCP原版red、89green/corepass、真实验证/汇总trace、自有进程停止审计、源码/镜像/更新和回滚证据，实际secret扫描且不覆盖v16。所有功能极致性能仍未实现，继续迭代。


### 2026-10-05：MCP已落地；隔离RPC增量成本测量待执行

MCP image bc85c186271873610ff759c008d5f36d49d1a9c331e064eb7fa1121d822485a3，编译778ad55708b1806d09c7691b245f870b17861983，二进制dbfef7cc761bc3253d4269038c63e1c347974c73574b5df1e07055fed201d926，实例bd8182dcd4be5ee219b58b4711fd5e4512a38b19c712ce5b7d0358b2bd36cef7。健康检查、现有token的schema50/100、完整ENV键值/命令/健康配置均通过；仅MCP更新，其他18/全部配置保持。原38dca/8cd229及精确rollback保留；正式principal1与AI问题仍未解决。v17本机65文件SHA通过，archive68a0b3dbbc8efaacfa0b69543778ede430646f658ba9c46dd278e722b4bfce57。

新组件诊断比较相同20ms受控等待/128B synthetic echo的direct6000次与真实gRPC6000次，各300/s20s；再仅父strace -c跟踪自己子进程1500次5s。CQ1/MIN1/MAX2匹配原默认，16驱动线程、3s每次RPC、35s各进程上限，所有计划请求不跳过。记录严格响应身份/内容/数量、迟发、各index对应的caller/handler wall及handler threadCPU/TID、rusage合计CPU与context-switch。只编译自己的文件和缓存proto/gRPC，排除全部其他TinyIMX实现archive和缓存MAX16 MessageServer；不构建正式目标或安装依赖。

不访问SQL/Redis/鉴权/真实用户/AI，不包含receiver/wire/10k在线背景。synthetic成功不是durable ACK或容量接受。rusage包括caller和server，strace计数包括启动关闭、等待时间并受ptrace干扰；不能用模拟结果直接替代真实Message CPU或归因全部延迟。用未跟踪成对计时和CPU测增量，以跟踪计数检验创建机制，保留共享主机顺序对比限制。源码/审计先Git，当前未测量，所有19/config保持；只在cmdline/starttime/PGID匹配时清理自己的新进程组。旧完整19身份压测工具因MCP更新不能直接重放。准备阶段自动审查超时未创建生成器/包/候选源，核验后单次重试；不是安全拒绝，无生产动作。目标未达，继续测量再选择优化。


### 2026-10-05：隔离RPC诊断首轮编译失败已保留，工具修正待测

5cd8b4b20826c8eec4d80fdae8c5b590b19f917f 的新工具在编译阶段失败：匿名命名空间 pb 别名与当前 Protobuf extension_set.h 的全局 pb 冲突。compile.log SHA44c558e859759be3a0729352b870f2b244eea2b1a53006d0303979c5d5153ddc，没有link/测量/容量结果。原失败目录不修改，单独只读归类确认全部19/配置保持、自有probe未运行；不能把编译失败当业务延迟或根因证据。

只把诊断别名改为 probe_message_proto 并全局限定协议命名空间；不修改任何生产实现。新attempt2目录/helper保留同样case和所有检查；编译/链接也使用自己的新进程组、180s期限和cmdline/starttime/PGID清理，INT/TERM/HUP已移至编译前，失败保存明确phase/exit/error及运行状态。全部原Git/日志/工具保留，v18尚未导出，将同时包含首轮失败、只读复核、两次源码迭代及attempt2完成结果。修正/审计先Git，当前尚未得到RPC增量测量；性能目标仍未达，继续测量。


### 2026-10-05：受控RPC机制证据与固定工作线程候选（尚未测量候选）

4b9db7f 的第二次组件诊断编译/链接与全部case完成，正式19容器和配置不变。direct/grpc各6000条合成响应全部身份/字段核对正确，无异常、无负的配对附加时间。相同20ms受控等待：direct调用P99 20.76237ms、均值20.26236，进程0.079237核、16个handler TID；gRPC调用P99 23.594134ms、均值21.92129，逐请求配对附加P99 3.226773ms/均值1.665207，进程0.698700核、484个handler TID。gRPC用户/系统CPU9.282559/4.704915秒，direct1.01423/0.571899秒；自愿切换65165/12004，非自愿420/15。两个进程均包括调用方与处理方，不能把差值直接当成正式Message独占CPU，更不能把23.6ms当真实持久化延迟。没有10k心跳/多跳/SQL/授权/发送接收确认，这只支持RPC开销机制存在。

父级strace 1500条合成调用全部正确，但显著扰动：调用P99 98.571528ms、计划迟到P99 1431.755516ms；681次clone3、16574次futex包括框架启动/停止，futex 77.65%为系统调用累计等待时间占比，绝不是独占CPU比例。不能用此轮traced数据作为性能结论或真实业务根因比例。

下一项为单独新诊断CPP：当前生成的CallbackService接口、固定16业务工作线程、待处理上限512，同一20ms处理逻辑/协议/客户端，A同步-B回调-B回调-A同步各20秒300/s，加父级callback strace5秒。阻塞工作只在自有工作线程，回调快速提交；Finish之后不访问reactor，OnDone删除，取消/期限在执行前检查。提交/完成/拒绝/队列峰值、处理TID、全量逐请求配对与迟到一起保存。尚未得到候选结果，尚不修改生产RPC实现或部署；先测机制再决定是否承担生产迁移的取消/期限/关闭/过载全功能风险。官方参考 https://grpc.io/docs/languages/cpp/callback/ 与 https://grpc.io/docs/languages/cpp/best_practices/ 。

v18首轮导出在写归档/审计之前被“Only bounded evidence files”拒绝：20MiB检查在ELF过滤前，自己的诊断二进制超过边界。保留原helper/原Git/原结果，单独新attempt2导出只把ELF魔数过滤提前，仍禁止二进制/秘密且普通证据上限20MiB不放宽。新的运行目录保存首轮失败分类和大文件类型/大小；修正导出需候选完成才能执行，当前尚未生成v18归档。所有失败与新源代码均纳入Git，性能目标仍未达。


### 2026-10-05：固定线程回调机制候选拒绝；下一步隔离连接往返成本

fb44d14完整ABBA各6000/6000字段正确，callback提交/完成均6000、拒绝0、结束待处理0、队列峰值2/3；全部19/配置保持。同步A1/A2进程CPU0.679041/0.714810核，回调B1/B2 0.870256/0.869075核；附加开销P99同步3.042336/3.324842ms、回调3.368641/3.365442ms，均值同步1.618209/1.709033ms、回调1.766088/1.760129ms；调用P99同步23.423174/23.807896、回调23.771882/23.734689ms。回调TID固定16而同步467/453，但CPU/尾延迟未改善，自愿切换回调91087/91388 vs同步65161/65073。因此仅处理线程数量下降不足以接受优化：拒绝向生产迁移此候选，也不能声称gRPC线程 churn 已解释真实188ms尾延迟。所有候选源码和失败判定保持，停止继续盲调此矩阵。

callback父级strace 1500/1500、clone3 44（先前同步traced681），调用P99 86.969201ms、计划迟到P99 276.326966ms；futex16035和78.40%仍是累计等待占比不是CPU。两次tracer扰动差异不能代替未traced性能；未traced回调CPU上升才是拒绝依据。正式消息同步handler/取消/期限/持久化均未改变。当前头文件明确同步/回调IsCancelled随时安全，CQ异步仅在AsyncNotifyWhenDone标签送达后安全，因此绝不能简单把旧handler塞进CQ并声称语义不变。

v18_attempt2归档100文件 SHAece928ff8e5251c4fd6cdb8b3d5533bd9c1e912e0dca8a708682b1f0ed51f4f8，包含首轮编译失败、修复、直接/gRPC、ABBA候选和首轮导出失败分类。Windows旧extractor已安全完整解包，但只接受v数字清单名而拒绝-attempt2后缀；保留该失败审计/目录，不删除或重新覆盖，只用准确指定清单逐文件复核，100个SHA全部通过，独立验证记录已保存。

只读审查确认MySQL池借用每次Ping且已经在池锁外，失败重连会归还slot，不是旧锁内网络错误。下一项另建诊断CPP，16专属健康连接，现有Message凭据只在进程内读取，地址换为真实MySQL Docker IP；只做SELECT1不读写用户表。固定300/s20秒的Ping+Query-A1、Query-B1、Query-B2、Ping+Query-A2，保存6000条逐请求Ping/Query/CPU/计划迟到与MySQL cgroup计数。native→Docker路径不同于正式container→Docker，且没有真实SQL/事务/10k心跳，Query-only只用于衡量一轮健康检查RTT成本，不是产品政策或故障恢复证明。当前尚未测量，不修改连接验证/重试/SQL/持久化/服务部署；机制收益确认后再设计空闲失效、出错后恢复、事务回收等产品验证。目标仍未达。


### 2026-10-05：减少确认路径SQL往返的原子收件人条件候选（尚未构建/测试/部署）

699b062只读健康连接ABBA四轮各6000条SELECT1正确。A1/A2 Ping平均0.561194/0.568582ms、P99 1.324623/1.354024ms；Ping+Query调用P99 2.368942/2.380542、均值1.149633/1.162640，Query-only P99 1.470426/1.556427、均值0.679460/0.680934。probeCPU A0.206279/0.204724核 vsB0.137156/0.138409；整个MySQL cgroup A0.248183/0.257747 vsB0.166127/0.170207（含后台/启动，不是探针独占）。每轮全部逐请求和资源计数保留，MySQL1/1/1/0/0不变，19服务/配置不变；native到Docker健康SELECT1成本不能当真实业务188ms延迟归因或10k性能通过。v19本地53文件逐SHA验证，归档97240a934d0e064d765d1d7badf0462ec66e3be561b551e6c65e5b9730d8e72e。

不更改连接池健康政策。跳过/缓存Ping会使已有断线测试从Acquire失败变为先拿到失效lease，语义变弱，当前候选保留每次健康检查。也不重复旧dc7083先查后更新单lease候选；新方案在成功Pending确认路径先执行收件人绑定原子UPDATE：PK message_id、to_user_id=认证收件人、status0、from_user_id非0、消息类型在当前枚举1..3、created_at非NULL。当前真实表是数值/日期类型，非NULL日期读取为非空字符串，其余字段原解析允许空值；所有原解析会拒绝的有效性条件在快路径保留，不加入额外的发送者!=收件人限制。影响1行才成功，0行在同一健康lease全记录查询，仍按原解析→不存在→所有权→Confirmed/Read/Failed/异常顺序分类。UPDATE错误不重试、不假报成功；0行后仍合法Pending视为存储不一致，保守失败。批量确认、已读、私信持久化、群消息和RPC取消/故障seam都不改；不降低事务/同步日志持久化。

真数据库回归新增：pool1同一会话SHOW SESSION Com_select前后差值证明成功Pending确认没有SELECT（初值不假设0）；消息类型0/4不更新；Confirmed/Read错收件人仍拒绝；合法和错误收件人并发只有合法者能迁移。原有状态9/Failed、不存在、重复、确认与Read交叉、4线程幂等、连接归还、原子outbox/回滚全部保留。会话计数只读前测曾错误要求初值0，实际mysql CLI初始1→SELECT后2；原失败/原始SHA729ce3a3338f992b9e67c0e0ebb5d8d32a95d5dc2678e814530269184cb166cc保留，独立只读复核差值1通过，不修改原失败。一次跨层命令参数错误发生在SSH/SQL之前，后改固定脚本/结构化SQL，无秘密输出或产品影响。

代码先审计/Git，当前尚未得到新候选构建或性能结果。构建只Message与必要测试、parallel1/缓存SDK，不部署Gateway当前fair源码；验证同步Server默认原源码重新编译，不能误封装缓存MAX16版本。新建两套精确命名独立schema池1/4（只读生产DDL，不拷贝生产行），所有测试写入/故障trigger/记录只在新schema，保存全部失败和schemas不DROP。测试通过后再独立审计镜像/Message替换和完全回退，匹配真实私信控制；目标仍未达，继续迭代。


### 2026-10-05：原子确认84构建通过；红对照记录工具修正，不改业务代码

84cb214e17bdd4590eb5763483e448d4f78b0d0b候选构建通过：MessageApp53、crash28、真实gRPC75，共156条PASS输出，无FAIL；默认同步服务对象不引用SetSyncServerOption，确认未封入缓存MAX16。候选Message SHA5d24b144577e01a5bfc7049b93ebcd65f61e1008da7e8513546ad0e041c55202，新SQLtest SHAe6eb32cf936a8e7e309fd14e3313c40c56999634a1170a2e177c1050f116ec75，原adapter新test红binary SHAbb0e16c38b2b3e3c842fb3b2734623f2f14b7ca16953b3b3892a2f88488f7582；实际编译版本84，与运行正式Message的ddc7不同。全部19/配置保持，尚未部署。

首轮独立MySQL红对照正确返回1，54条独立功能断言PASS，只有“Pending normal path avoids SELECT before update”新行为失败；末尾另外打印“[FAIL] M16-A Transactional Outbox integration tests, failed=1”总汇。工具把两行FAIL当两个实际断言，导致停止在红对照，绿候选没有运行。原failed SHA bb82f8aa89bed78669c8ba8edb3633dd9831e966b9dc83524cd78d478118c0c9、原log SHA d379d78888f1440fa515bcd826ff10546da41f97b712fffcad9ccae15f585265保持。只读复核核对真实创建的schema只有codex_receiver_guard_20261005_red1；旧failed字段列的是3个计划名，不能说尚未创建的green1/4已保存。此错误是测试汇总分类，不能声称业务候选失败或已通过绿验证。

只修复helper，产品和testC++保持84逐diff不变；新版将实际断言和准确总汇分开，要求红对照恰好1个预期失败且总汇failed=1/exit1，绿对照必须完全无FAIL输出和exit0，未放宽任何业务断言。新attempt2目录和3个全新schema red1a2/p1a2/p4a2，保留首轮全部数据不删除/复用。现有生产created_at实际TIMESTAMP NOT NULL，独立DDL前检查已按实际typed TIMESTAMP/DATETIME处理，并非臆测字符串列。helper/docs新Git与编译84区分，使用二进制前只允许docs/evidence_tools差异，若业务/test/CMake变化则拒绝。v20将在真正新红绿完成后冻结全部源码/构建/首轮失败/复核/第二轮结果和会话计数失败/复核；当前新绿结果仍NOT_RUN，整体性能目标未达。


### 2026-10-05：原子收件人更新真实数据库绿验证完成；准备匹配性能对照

163f4df 的新工具完成红绿对照，实际编译代码仍为84cb214e17bdd4590eb5763483e448d4f78b0d0b。原版红对照54条独立PASS，恰好1条避免SELECT的新行为FAIL，准确总汇failed=1/exit1；候选池1 outbox55条PASS、unread snapshot14条PASS，候选池4 outbox54条PASS，共123条绿PASS且无FAIL/exit0。与此前156条单元/真实RPC PASS分开记录，不把红对照或零输出测试伪算进绿数量。三套独立red1a2/p1a2/p4a2 schema和首轮red1都保留；生产SQL配置1/1/1/0/0、全部19服务与配置不变，尚未部署或证明性能改善。v20归档77文件SHA d531f77096f86137af9fc42e8b1da0989e8b0ccdf8f24f09e3a3b4281d0f1589，Windows新目录逐SHA全部验证。

方向依据是正常Pending确认去掉一个健康lease/Ping及完整行GET，不缓存/跳过现有健康检查，不降低持久化或期限。首次成功原子UPDATE由PK、收件人、Pending、非零发送者、有效消息类型和日期绑定；重复、已读、错误身份和异常数据仍完整解析分类。终态/重复路径现在UPDATE零行再GET，可能比原版GET更贵，需要后续重复确认及混合交互负载验证。发送者positive ACK边界不直接包含收件人确认，此修改可能通过降低共享CPU/SQL竞争改善端到端，不能把确认路径省时直接当ACK省时。

本次源代码仅新增封装/部署/同负载运行/分析/精确回滚/安全导出helper，C++与测试仍84未改。镜像只替换Message二进制5d24b144577e01a5bfc7049b93ebcd65f61e1008da7e8513546ad0e041c55202；运行Gateway仍33fc原版1d8/16线程，User仍原38，已修复MCPbc85保留；不能部署当前Gateway拒绝过的fair源码。原版先测guard150A1，10k登录100人/秒、150私信/秒、60秒/9000计划、3秒原期限/15秒心跳；只换Message后guard150B1同参数。全部登录、计划/尝试/positive/wire/SQL确认、无skip/negative/late/disconnect、心跳完全相等，以及原100ms两项P99门槛不改。只读两帧SQLdigest/19cgroup/guest压力计数限定在活动窗口，保存观察开销；后台与时序共享主机限制明确，部分/失败人口不得全负载归因。全部结果包括FAIL保留，不在构建时测试、不因减少线程/查询就接受优化。当前控制尚NOT_RUN；即便私信改善，群组/离线/文件/搜索/MCP/AI/20k至50k/故障及长稳仍未完成。

部署前保存真实inspect私有备份、原image/tag与rollback override，仅Message可重建；环境键值、命令、healthcheck、配置SHA、其他18 ID/image/start以及实际容器内二进制校验。失败或回归按证据恢复原b24并核验健康/二进制c119，保留候选镜像、所有自己的停止探针和数据库行。不删除文件/容器/数据，不清全局缓存，不停任何其他应用，不改VM/主机安全/网络；运行前内存门槛只用于避免失真的负载，保留用户游戏/Python。

本地准备也记入失败链：red-review生成器第一次未闭合三引号导致Python解析失败，在任何写入之前停止；随后的package因新源文件不存在而拒绝，未上传/改业务。修正时一次自动权限审查超时，只读确认没有修改后一次重试完成；这是超时，不是拒绝安全请求，不代表业务故障。原本地失败审计保持。Git提交、编译版本、运行镜像以及功能通过/性能待测分别记录，继续按实测结果迭代。


### 2026-10-05：原子确认机制生效但端到端收益不足，恢复原运行版并转向在线共享开销审查

4240e2c 已保存封装/部署/对照/分析/精确回滚脚本。候选镜像e38099c18b81d67a64a1daddb63171c01c9681cb4e826465f91055dd86b0e4fd仅含Message84cb214/二进制5d24，UID1000加载和受控缺配置退出通过；原版guard150A1先运行后只替换Message执行guard150B1。两轮均10k全部登录、9000计划/发送/positive ACK/实际wire/SQL确认，skip/late/negative/disconnect0、所有worker完成，心跳82754/83102各完全相等。原版P99 175.5ms/计划到ACK177.5、max308.782、活动149.9333/s；候选P99 173.1/174.8、max288.524、活动149.9167/s。两项原100ms门槛都FAIL，退出2原始保存；2.4ms差值在共享主机顺序测量中不足以证明稳定优化，不追加盲目参数矩阵、不声称解决主要瓶颈。

两轮只读活动计数窗口有效58.3712/58.4051秒，原两帧观察350.570/291.202ms，候选393.774/319.268ms，完整开销保存。按MID完整行GET 21922→13154，INSERT8748/8749，GET/INSERT约2.506→1.503，与成功确认少一次GET机制一致；不能把活动计数窗口8749条当9000全窗、背景也不可称独占。Message平均CPU0.905532→0.852731核，系统0.510398→0.446024、用户0.395134→0.406707；MySQL1.430540→1.430536核几乎不变；COMMIT9350/9399次、均值6.944610→7.322538ms，确认UPDATE8751次/8.915323ms→8746次/10.715598ms（新SQLdigest不同，计数细微边界差异）。两GW各约0.71→0.72核、Social0.31465→0.31928、Redis0.58878→0.55795、nginx0.26571→0.26918；全19无自身cgroup限流。查询次数下降是真实机制证据，提交/确认等待和MySQLCPU未改善；所有平均值不是SQLP99，也不能把总时间求和当独占墙时或推导因果百分比。

运行原Messageb24/c119已按精确回滚恢复，当前CIDcbac27adc561012c78a6bfd91a86b2860e89bf7a8a1a285e193993f690485293，健康、pool16、配置2ead、ENV键值/命令/healthcheck/其他18 ID-image-start和1/1/1/0/0都核验。新MCPbc85继续保持。候选源代码84/测试/编译二进制/镜像/SQLschemas/所有消息/raw仍留存，当前GitHEAD不能冒充运行代码；本次未修改产品C++或测试。v21归档118文件 SHA48f7da3c587c3b0f454e0feececbc322e11a44c7d2086561be1b3a34a258120c，Windows新目录119归档条目/118逐文件SHA通过，包括两轮FAIL、部署与恢复，没有配置、环境、ELF或完整服务日志。

下一项先只读完整9000成对台账的发送→ACK、发送→wire、wire与ACK时间差，原直方图P50/P95/P99/P99.9和调度迟到不加额外0.1ms；分析guest CPU/PSI增量，明确观察帧捕获边界和同机客户端/代理/其他应用共享CPU限制。负载生成器已有心跳优先队列，不存在每循环扫描所有10k心跳的旧问题；固定epoll_wait10ms会造成若干发送合并，已有计划到ACK和调度迟到严格保留，不能用改发送节奏隐藏服务不足。只读源快照取实际运行Gateway33fc、Message ddc关联公共连接实现及当前HEAD作为审查上下文；当前Gateway公平调度源码曾拒绝，严禁直接构建部署它，未标注Git的Social/outbox运行等同HEAD也不能臆测。重点检查持续心跳/在线路由以及每私信好友关系校验与共享网络往返，再决定新产品变更。当前全部功能极致性能及20k50k仍未实现。

本轮分析证据限制也保存：回滚前候选服务阶段日志未单独采集，容器重建后不可用，不能捏造候选phase或比较；所有必要完整台账/数据库/CPU/压力/门槛原始结果已保留，原版日志在部署前私有快照中可筛出数值慢样本（阈值且限频，有偏样本非P99）。新版回滚helper仅增强今后的模板，在重建前把本任务当前candidate log保存runtime-private，捕获失败仍保存分类并继续必要回滚，绝不导出全日志；已经执行的旧Git/原回滚记录不修改。曾只读误查resources.jsonl，实际是guest-resources.log，改用真实路径后读取，未更改任何样本/产品。主机其他应用/游戏/Python全部保留，无全局清理或VM安全更改。


### 2026-10-05：完整投递与共同消息计时证明等待分散，先验证在线维护合并机制

9eb6296 的只读成本报告完成。原/候选全部9000发送-ACK-wire按CID/MID和双方身份成对核对，无丢弃样本：发送→wire均值74.6993/72.6494ms，P99 210.1270/210.9084、P99.9 286.9274/296.8753、max324.5814/358.2285，实际投递尾延迟没有改善。2554/2604条实际delivery早于sender ACK属于允许交叉顺序，未把负wire-minus-ACK时间丢掉；该差值均值10.5391/10.1802，P99 73.4677/73.2296。原完整ACK均值64.1773/P50 55.4/P95 130.1/P99 175.5/P99.9 245.6，候选62.4872/54.6/125.3/173.1/240.3，严格原100us上界桶保持。发送调度迟到均值1.71312/1.73115、P99 9.1/9.0、max15.383/12.035ms，不能把近175ms尾延迟都归因于10ms客户端epoll等待。

活动guest忙碌85.6950/85.1040%，system+softirq40.1613/39.1819%，ctxt37885.5/37278.7每秒、guestwide fork/thread310.821/312.832每秒（包含全部应用/观察者）。CPU some压力63.2608/62.3974%，IO some2.0670/2.0806/full1.0182/1.0235%，memory压力接近0；19cgroup总5.42436/5.37003核（不含所有系统路径，不能与guest差值断言loadgen独占）。采样在两帧中读数时刻有350-394ms边界不确定，所有capture值和原stat/PSI保存。这不是内存不足导致本轮失败，不进行全局内存清理、主机程序停止或安全/VM设置更改。

原版488个阈值/限频持久化慢样本全部按MID/身份/单调时间与同一条消息台账匹配，全部计时顺序有效，没有不匹配。这个共同有偏样本发送→ACK均值74.79054，由进入仓储前33.66787、仓储内部23.44657、仓储结束至ACK17.67611ms精确逐消息加和；commit均值11.31337、max110.503ms。仓储前包括client/nginx/Gateway排队、权限/路由与RPC，仓储后包括服务返回/RPC、未读快照和ACK交付，不能直接称某一项独占。此样本不是总体P99，也不把不同人群均值相减得出差异。

Gateway原版慢chat日志327条、candidate299，均失败0；原chat dispatch年龄平均11.2933、work119.8545、permission19.2139、route8.16086、persistRPC82.8792、unread8.87533ms；candidate9.57197/118.8080/17.3080/8.03041/81.7788/11.14788。peer原4/candidate6条慢样本，不是完整交叉路径总体。进一步原327 Gateway与488repo取相同MID交集40条，全部RPCduration>=repository：同一组senderACK均值152.6363，Gatewaydispatch10.9378、权限17.03325、路由8.941075、persistRPC87.349325，其中repo59.627375/RPC之外27.72195、unread10.528375，repo内commit34.307525；40条是两处慢采样交集，不能称主观百分比根因或将27.72当gRPC独占CPU/运输时间。真实尾等待横跨存储与多跳/共享调度，需要改变高频成本而非只调单个SQL参数。

已按运行33fc源码确认：Pong快速返回、4线程异步presence、O(1)session反向索引、本地ZK发现快照、权限一条双向SQL、原子未读一lease/EVAL快照、持久化确认边界之前的sender ACK策略都已存在，不能重复声称修复。在线刷新仍每次健康lease/PING再owner(gateway_id+connection_name)绑定Lua GET/JSON/EXPIRE。10k/15秒约667次维护每秒，再叠加每消息路线和未读Redis调用；下一项只能先隔离机制：独立自有Redis键，原single Lua与有界批量owner条件刷新，相同open-loop维护人口/健康检查/状态结果，验证TTL、旧连接/新所有者、notfound/invalidrecord/wrongtype/取消或停机边界，再比较网络命令数/CPU/墙时/迟到。当前尚未编写/构建/测量生产批量候选，尚不承诺端到端收益，native→Docker组件路径不同于真实container→Docker；不缓存/放宽好友拉黑权限，不跳过健康检查，不延长客户端期限或削弱持久化。

日志当前Gateway/Message/Social均warn，console=true/file=true/async=false/file每条flush=false；User info但活动User仅0.023核。Logger代码同步锁/console endl存在，TINYIMX_LOG确有过滤前ostringstream构造，但正常INFO实际写入在上述服务已关闭，异步日志改造不是已证实主要瓶颈；不能直接调更低级别或根据源码就宣称根因。v22本地44条目/43文件SHA全部验证，归档a44d03d3ac977d267d0ac900ddb38256f0728281e46905439e88f10a4ddb2f47；Gateway数值3文件单独哈希下载核验，仓储与GatewayWindows数值join源工具与结果均单独留存并原样复制至guest新stage，Python执行环境为原Windows任务tools目录，Git forensic copy不在guest执行。

只读解析失败与修正也记录：第一helper错把peer context user_id当recipient，嵌入Python35行AssertionError，第一stage只有审计无成功数值报告，原脚本/stage不覆盖。运行33fc的SubmitMustRunConnectionBusinessTask不赋user/epoch，BusinessRequestContext默认0，HandleGatewayForwardChat使用该helper且ScopedBusinessDispatchContext(connection,0,0)；只读分类sourceSHA4a8fed3bbc7decc120eb287d9022ba0023c7ab3c52d551d019e3fa0c786eb5da、类型SHAabe8fc9ff0181e5a91cd8234a316d9ac13eac894e6413efa9c11b5809b743228。新attempt2保留MID绑定正ACK台账；chat sender/原seq/正epoch，peer内部user=epoch=0/正seq，产品身份校验没有改。分类命令一次自动审查超时未启动，仅一次同意重试成功；这是超时而不是安全拒绝，没有approval阻塞。全部19/config/运行原版及MCP好修复仍保持、整体目标未达，继续有证据地迭代。


### 2026-10-05：隔离在线维护原型的审计与执行前状态

只读预检 `online-maintenance-mechanism-preflight-20261005` 完成于7e93560：实际Redis7.4.11、standalone cluster_enabled0、noeviction、maxmemory0/maxclients10000，连接25/阻塞0/used107636592B。真实native目标172.18.0.13，两个Gateway Redis pool8，运行33fc的owner条件刷新Lua与当前源码一致，SHA12b8fcb43720694347e0a781d9c7ed6adcff43bdb34c37e750cf2c38e26246b1。原池Acquire仍健康PING，没有跳过；两组相同4worker仅是受控逻辑Gateway模型，不把组件等同完整10k心跳。主机可用约5.5GiB，无需清理；其他应用、游戏、Python保持。

本次只添加诊断CPP，使用既有redis_pool_demo编译flags与link精确SHA和项目静态库SHA，仅编译自有object/ELF，不构建或部署任何产品服务。固定自有Redis前缀codex:online-maintenance-probe-20261005:，15类x2模式合法所有者/缺失/旧网关/旧连接/损坏JSON/scalar/null/缺少所有者/零负非法TTL/wrongtype/替换所有者/unicode/已过期，逐项状态+TTL+原字节预计84检查。原Lua完整同体包装，批量每key独立pcall，wrongtype错误只对应该key，其他结果继续；尚非故障网络/产品取消与停机正确性验证。功能通过后才创建10000全新自有键TTL300，不引用生产键、没有DEL/FLUSH/CONFIG。正常所有者EXPIRE120，失败不改记录/TTL，所有夹具自然到期；不能任意删除历史证据或清理内存。

随后single-A1/batch-B1/batch-B2/single-A2，各667/s20秒、13340全部计划槽open-loop，同8worker/两pool8，batch两collector/最多16key/最长5ms合并。两512有界队列溢出即保留失败，不丢样本；要求planned/attempted/issued/completed完全一致、错误0、逐ID/状态1/时间顺序核对。CPU包括collectors，native到Docker与产品container路径不同；Redis cgroup含背景/.5s屏障/结果序列化，不能当独占业务CPU。编译链接180秒、prepare60秒、各case90秒，仅PID-starttime-cmdline-PGID匹配的本任务子进程组可在失败后终止；所有日志/live/partialraw/failure阶段保留。19运行ID/image/start、私有配置SHA逐项保持，SQL未触及，原持久化1/1/1/0/0不变。

执行前结果明确 `NOT_RUN`，不得称生产批量候选或全功能压测通过；命令减少之外，还需CPU、迟到/5ms维护新鲜度与全人口错误证明。如果机制有效再做生产生命周期/所有权交叉测试并审计正确运行基线，不直接构建源码中已拒绝Gateway fair调度。运行仍原Gateway33fc/1d8、Message ddc7/b24/c119/CIDcbac，User38、MCPbc85；源码guarded确认84与当前Git不等于运行版本。v23归档24文件SHA88f9c20c3d930d05323330493a241e3b0a7ee2c48729e8e6079c9b654f2f3290，已本地逐文件验证；v24预置仅完整组件证据才导出，排除秘密配置/env/私有日志/ELF并RAM秘密扫描。审查时在打包前修复漏standard include、临时vector迭代器错误以及RedisConfig.enable必须读取真实配置；无执行或产品变更。曾只读猜错services/gateway路径，真实gateway/由rg文件清单确认，不是产品失败。持续迭代，所有功能10k50k极致要求仍未达到。


### 2026-10-05：在线维护机制有重复成本收益，新增有界缓存API前置测试

c87b9b5实际执行 `online-maintenance-component-probe-20261005`：84项检查全部通过；ABBA原/批/批/原各13340计划、attempted、issued、completed、逐ID结果1完全一致，错误0，全19/config保持。原A1/A2 EVAL与健康lease各13340，batchB1/B2各6622，最大批3；原nativeCPU0.362273/0.368229核、batch0.335281/0.336015（包括两collector）下降约7.45%/8.75%。全Redis cgroup原0.528632/0.600619、batch0.291491/0.361036核，两组对照分别下降0.23714/0.23958核；该cgroup包含背景/.5sbarrier/序列化，不能称独占业务CPU。原enqueue→result P992.611634/2.919340，batch8.001407/8.092809ms；scheduled→result P992.854266/3.255612→8.302184/8.445341，5ms合并增加异步维护新鲜度等待。只有native独立测试键，没有真实10kPong/消息混合压力，不等同完整性能验收，也不外推50k结果。

v24归档87文件/88条目 SHAa7af2aed2df360cd2495d0d4329a83d1e2e7faade9356bd8a8f317d2ce2603a2，全87逐文件SHA本地通过，含53360全部原始请求与语义/CPU/进程/编译审计。全服务/资源/应用仍原样，无内存清理必要。下一阶段不重复容量压测，先把有证据机制实现成缓存API，保证功能衔接再接入Gateway。

只读基线snapshot确认Gateway/相关cache/example相对运行33fc仅BusinessExecutor.cpp有已拒绝6521 fair调度差异；本次仅恢复该源码精确原SHA3b707e2e5a63cd7671a92c977ea75fc7fcaad29de1c90e92e41bcb8f9017cb22，Git父版本/审计preimage仍保留，没有构建部署它。新增 `OnlineStatusRefreshRequest` 与 `RefreshOnlineIfMatchBatch` 最大16，输入顺序返回、逐项原校验（非法项不触及Redis），全非法/空不Acquire，超过16整批invalid且无I/O；合法子集仅一次原健康Acquire/PING和EVAL。Lua包装一次从原single body生成（原SHA12b8...不改），每key pcall隔离wrongtype，原owner网关+连接/GET/JSON/EXPIRE和逐项typed状态、TTL/原字节保留。当前Redisstandalone范围明确，不声称提供RedisCluster跨slot批量能力，不放宽好友/持久化/超时或加入盲重试。

API真实Redis测试CPP与helper分别ownpool1/4，用全新固定prefix codex:online-maintenance-batch-api-20261005:p1/p4，最多120自有键TTL300/专用1秒过期/正常120刷新，显式替换自有夹具，禁止生产键/DEL/FLUSH/CONFIG/SQL。18类原单条与批量逐项状态/TTL/字节对比、wrongtype后有效项、空/1/16/17边界、同用户混合所有者顺序、替换后旧owner不得续期、null/uninit/只关闭本任务池及invalid项、4并发调用100个三项mixed batches/全部lease归还。每检查保存JSON，failure/编译/log/partial原样留存；编译ownAPI.o+test.o/link各180s，cases90s仅本任务PID身份匹配组可终止。结果在提交时明确 `NOT_RUN`，没有全局Redis故障或产品Gateway生命周期测试；不把真实API functional与之前prototype84混称。

接入审查发现原网关kNotFound会验证当前local session并自恢复，必须保留；不能把批量NotFound直接丢弃而制造在线路由丢失。还需执行时epoch+connection当前性、立即Pong、错所有者/下线交叉、queue上限/结果终态、停机排空后销毁pool。拒绝让四个presence调用线程同步等5ms形成max4批，因为50k用户时吞吐会受工作线程数量限制；后续只考虑独立有界异步批量调度及实际生命周期测试。当前API尚未在Gateway使用，运行仍33fc/1d8、Message原ddc/b24/c119、User38、MCPbc85。所有功能极致及20k50k要求仍未达到，继续按证据迭代。


### 2026-10-05：批量缓存API350检查通过，先验证命令无响应的停机边界

cca0e3b实际 `online-maintenance-batch-api-20261005`：ownpool1/4各175检查全部PASS，共350；两组原single18与batch18逐项对比、TTL/原字节/所有权及fault结果保留，size0/1/16/17、wrongtype后的合法项、重复用户旧/正确owner顺序、替换后旧owner、null/uninit/只关闭自有pool、4并发100个mixed3item batch/全lease归还都通过。原运行19身份/config/健康核对完全一致；API自有编译object/ELF留存，没有构建或部署Gateway。v25归档41文件/42条目 SHA35524c79c6a3de63e399515ae229071ceb9e819fde280748a1a356800994cce8，全41逐文件SHA本地通过。此前prototype84与实际API350是不同检查，不能混称全功能或Pong/50k验收。

继续审查异步合批生命周期发现代码仅redisConnectWithTimeout3s，实际安装Hiredis1.3.0 header区分command_timeout并提供redisSetTimeout；同版本官方primary源码 https://github.com/redis/hiredis/blob/v1.3.0/hiredis.c 中redisConnectWithTimeout只设置connect_timeout、redisSetTimeout设置blocking socket读写边界。当前SDK读取原RedisConnection源码与运行33fc精确一致。本次先诊断，不因源码没有调用setter就直接声称实际无限等待，更不把故障停机边界当正常175ms根因。

新诊断CPP只连接任务自有127.0.0.1 OS分配高端口模拟端点，禁止6379/11434，不读取真实凭证或连接productionRedis。AUTH固定public-owned-auth-fixture属于公开测试字串，SELECT1/PING/EVAL return1无生产键。每类healthy先正常+OK/PONG/:1证明协议/客户端路径，随后ownpeer在收到正确command后故意4.25秒无响应，再仅关闭自有peer释放客户端。记录原进程是否在连接3秒之外仍未完成及全部Native/服务端单调时间；预期red4次bound violation，编译前结果明确 `NOT_RUN`，要真实复现后再单独审计修复。共8顺序case，1native child/1serverthread/listener/peer，编译link180s、每case20s安全guard，仅PID身份匹配的自有组可在失败时终止。审查先修正finally关闭次序：释放peer/关闭listener后，必要时终止自有child再等待serverthread，避免异常时阻塞接收让threadjoin抢先失败。没有执行失败或产品改动。

后续若证实缺少command边界，有限I/O超时属于故障正确性前置，不延长客户端3秒期限、不跳过healthyPING、不盲重试。再验证所有原API350及健康AUTH/SELECT/PING/EVAL，保留red/green原始证据与Git后才接入bounded async collector/workers、即时Pong/current epoch/connection/旧会话取消/停机排空。另有kNotFound→current local check→SetOnline的两阶段恢复窗口需自有交叉用例验证，不能在批量中省略自恢复，也不能臆造已复现的真实远端替换故障。所有运行仍GWs33fc/1d8、Message原ddc/b24/c119、User38、MCPbc85，源码executor已恢复运行版、cacheAPI尚未使用；全应用/game/Python保留，无全局清理。完整所有功能10k50k极致目标仍未达到，继续按证据迭代。


### 2026-10-05：原Redis命令无响应缺少边界已复现，修复单次I/O空等上限

f036ee5 `redis-command-timeout-original-probe-20261005`实际完成：AUTH/SELECT/PING/EVAL四healthy正常成功，四无响应全部在4.25秒屏障仍未退出，关闭自有peer后才失败。原Ping4.252310381/Auth4.252681309/Select4.251610326/Eval4.252164753秒，错误均Server closed the connection；AUTH/SELECT连接建立完成但初始化命令最终失败，PING/EVAL原Connect成功。自己的loopback源/peer/command接收与释放单调时刻/InstalledHiredis1.3.0/cachedlibSHA全部保存；没有真实Redis访问、生产故障/配置或键写入。v26归档70文件/71条目 SHA96fdcc172dc16464c3fe99b86b39b62dcf908fe1d4394fb4e869721bffde2d58，本地70逐文件SHA通过。源码判断由实际cachedSDK行为支持，但不能解释正常负载175ms全部尾延迟。

本轮只在成功建立socket后、AUTH/SELECT之前调用redisSetTimeout(context_,原timeval3s)，失败关闭context并返回false。仍保留redisConnectWithTimeout3s、原健康PING/重连/返回值/协议与解析、原客户端/RPC期限，没有延长门槛或重试。Hiredis1.3.0 setter配置blocking socket单次读写空等上限；不是整个命令绝对期限，分段回复drip、发送缓冲阻塞和全局停机deadline尚未实证，不得把3秒直接当全操作上限。官方同版本说明 https://github.com/redis/hiredis/blob/v1.3.0/hiredis.c 。当前仅源码候选、没有产品部署。

新fixedstage编译自有RedisConnection.o置于原cachedlibs前解析class符号，所有原SDK/cached产品ELF不覆盖；同一个已提交surrogate probe源码重建，四healthy和四无响应严格要求idle失败且连接不可用、2.7..4.25秒、peer关闭之前已返回，避免把EOF误算timeout。随后exact350实际APIharness仅固定namespace/stage字符串替换到新自有regressionCPP，原测试源码不变且原/生成SHA保存；编译ownAPI/test/Connection objects后用新prefix codex:redis-command-timeout-api-regression-20261005:p1/p4，pool1/4各175+100并发mixed batch，最多120newownedkeys/TTL300/1expiry/正常120/自有replace。Privateconfig仅RAM，禁止生产key/DEL/FLUSH/CONFIG/SQL，全部19/健康/config保持。所有编译/partial/logs/failure/PID-身份审计保留，执行前green+reg明确NOT_RUN。

异步Gateway合批尚待开发与正确性测试：有界input/ready/outstanding人口、执行时会话epoch+connection、Pong即时、result终态计数、队列deadline与取消、kNotFound安全恢复、关闭后排空及cache/pool销毁顺序；不能根据socketidle setter就声称全局停止保证。所有游戏/Python/其他应用保留，无内存清理必要。Gateway源码executor已经恢复运行33fc，cachebatchAPI350已证实但运行仍未使用；运行GWs1d8/33fc、Message原b24/ddc/c119、User38、MCPbc85，持久化1/1/1/0/0未触及。所有功能10k50k极致目标继续推进，当前未达到。


### 2026-10-05：命令idle修复验证完成，复现在线自恢复的交叉窗口

5063423实际 `redis-command-timeout-fixed-probe-20261005`全部通过：四healthy AUTH/SELECT/PING/EVAL成功；四无响应在ownpeer释放前自行idle失败，Ping3.051162740/Auth3.020139256/Select3.043109987/Eval3.008081565秒，错误err1/Resource temporarily unavailable，连接不可用，不能误算peer EOF导致结束。随后新prefix350原API回归各pool175全部PASS、各100并发mixedbatches、全部lease归还；原/生成harness只stage/prefix替换，原测试源码没有改。所有19身份/config/health保持，尚未部署候选。v27归档99文件/100条目 SHA615a7220afc5b3aae56382ea7d68dd9d90d330a7edafd945b076dbd4bf0fa5b1，全99本地逐文件SHA通过。3秒是单次blockingI/O空等，不是wholecommand/RPC/partialdrip/全局停机绝对期限，TXstall/drip尚未验证，没有正常175ms根因或完整容量宣称。

后续Gateway异步合批审查查实原HandleHeartbeat传ordering_key std::nullopt；原gateway.presence.offline设置user/epoch/kMustRun但没有ordering_key。因此不能假定既有presence四worker会把同一用户刷新/下线严格串行；SubmitSessionBusinessTask仅复制传入optional key，没有隐式排序。缺失记录恢复是Lua返回NotFound→检查local current→SetUserOnline→unconditional SETEX，检查与写入不是同一原子操作。

本次仅新增诊断，使用已350验证的actualcacheAPI.o与finiteidleConnection.o，objects SHA冻结、cache/header source与编译输入SHA核对，原SDK/cachedELF不覆盖。独立namespace codex:online-restore-interleave-probe-20261005:60001..60004四个全新自有键，TTLs120/300。四个确定性交叉case：缺失直接恢复control；localcheck后remote owner先SetOnline、旧SetOnline再写；localcheck后模拟unbind/current=false及ownmissingOfflineCleanup、旧SetOnline再写；原restore先写remote再写的反序control。Expectedred两窗口、healthy两控制，逐个记录typed结果/完整自有record/操作顺序；明确local-current是模型boolean，不操作或声称验证真实TCP/Gateway会话、不估计线上竞态频率或性能。ownSetOfflineIfMatch只对原missing夹具返回NotFound，不执行删除；没有生产key/直接DEL/FLUSH/CONFIG/SQL/login/服务修改。

执行前结果明确NOT_RUN。若复现，应单独审计missing-only原子Lua避免覆盖并发新owner，且给恢复与下线设置共同的user ordering/fence，保留oldconn/epoch取消和kMustRun清理；单独SETNX不能解决“cleanup之后旧恢复重新创建”的本地生命周期窗口。异步刷新不得在独立batch线程直接blind自恢复，应把恢复交回有会话取消和用户排序的presence域，再验证断线/替换/重连/队列deadline/shutdown计数。后续有界input/ready/maxoutstanding/worker collector和即时Pong仍未实现，没有极致全功能性能验收。生产仍GWs33fc/1d8、Message原ddc/b24/c119、User38、MCPbc85，所有apps/game/Python保持，无资源清理或VM/security/NIC修改。继续按证据迭代。


### 2026-10-05：缺失presence恢复保留新owner，刷新与下线共享用户排序

648ca58 actualcacheAPI窗口诊断四个自有键：正常恢复与反序remote写两控制正确；remote先写/oldrestore后写覆盖新owner、cleanup先结束/oldrestore后写重建旧记录两原问题确定性复现。local-current是明确模型布尔量，未验证真实Gateway/TCP频率。v28冻结28文件/29条目 SHAbf1f42fabdfd3773a82bf3093caaaf1cca4194ed68e3846caa7962ae7d48b4c7、本地全28 SHA通过。

本轮11路径修改前审计/原文件备份，以648为parent，添加单key原子SetOnlineIfMissing(SET EX NX Lua)，SetOnlineStatus新增kAlreadyExists尾值3、原Stored0/Invalid1/RedisError2保持，合法login原SetOnline不改；任何既有owner/坏JSON/错类型byte与TTL保留。Gateway仅missing heartbeat自恢复改用新API，失败typed告警；即时Pong保持。Heartbeat取消任务与kMustRun离线cleanup显式使用同一PresenceUserOrderingKey(userID)，保持sessionepoch/connection取消，cleanup解绑后必须执行；原restored33fc执行器调度不改。SETNX解决远端owner窗口，共同用户排序/取消解决本地下线之后重建窗口，两者缺一不可。未来异步batch线程不允许直接missing blindrestore，必须回交同域用户排序和session取消任务。

新增独立actualRedis缓存测试，privatepool1/4、全新固定namespace、最多160键、TTL300/120/expiry1；现有owner/无效记录/错类型bytes与TTL、invalid/null/uninit/ownshutdown、只有一个竞争恢复赢家、两交叉窗口；真实restoredBusinessExecutor+ThreadPool自有编译对象测试active restore→mustRun cleanup、cleanup→queued stale restore cancel及accepted终态计数，local-current仍明确模型，不能当真实Gateway会话验收。只允许explicit ownfixture SetOfflineIfMatch生命周期清理，不涉及其他记录/直接DEL/FLUSH/CONFIG/SQL。原350APIharness仅freshstage/prefix替换并冻结源/生成SHA，复验缓存API扩展影响。全部编译/链接180s、测试90s有ownPID/starttime/PGID/cmdline守护，失败/部分结果保留。

提交时测试NOT_RUN，完整Gateway构建及真实会话交叉回归、异步有界队列生命周期、匹配150/s压力尚待执行；生产19身份/config/health均保持，GWs33fc/1d8、Message原ddc/b24/c119、User38/MCPbc85，durability1/1/1/0/0保持。命令3sec仅perI/O idle，不是wholecmd/partialdrip/全局停机绝对时限；先前4healthy/4stall+350API已绿但未部署。所有apps/game/Python保留，不清理/删文件/改VM/security/NIC，不重跑已拒绝callback/worker32/groupcommitdelay或跳过healthyPing。10k50k所有功能性能尚未达预期，继续凭证据迭代。


### 2026-10-05：deef66c安全恢复136+350全部通过，接入默认关闭的有界在线维护合批

实际online-restore-missing-fence-test-20261005 pool1/4各68、合136全部PASS；original350 APIharness只freshstage/prefix替换各175全部PASS。真实Cache/33fcExecutor/ThreadPool编译自有对象，remote owner写后oldmissing restore保留bytes/TTL、8竞争仅1winner、active oldrestore后mustRun cleanup最终missing、cleanup先active后的queuedstale取消且0IO、排队deadline过期0IO、accepted终态2/2归零。local-current依旧受控模型，不代替完整Gateway真实会话。v29冻结70文件/71条目 SHA88db90de3b379b64facbc8489a4a1bec977e496a675226648a22c9160dec1d36，本地70逐文件SHA全部通过。

本轮11路径预审计后添加headeronly OnlineStatusMaintenance：单collector、4worker/max16/5ms；总pending包含input、ready、执行/callback，不允许独立队列突破现有presence容量；TrySubmit不等Redis。10sec beforeI/Odeadline，epoch/connection current前置及callback前取消。所有accepted只记一个completed/cancel-beforeIO/cancel-beforecallback/deadline-beforeIO/workerexception/completionexception终态；typedRedis结果独立计数，不重复terminal。worker只ownerchecked EXPIRE、不创建记录，NotFound回交原presence userordering/cancelable RefreshUserOnlineIfMatch→missing-onlyNX；cleanup原kMustRun保持；恢复提交继承原心跳received时间防止重置预算。即时Pong保持。

默认关闭，只有exact TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE=1开启；Bootstrap loop结束先Stop维护并join所有worker/callback，再drainpresence/cache，Gateway.Stop/析构幂等补偿。Stop不声称绝对30sec；Redis3sec仍只是I/Oidle，部分响应/全局截止未证明。新增realcache生命周期受控测试包含capacity/inflight/ready上限、shutdownadmission、cancel/deadline/exception隔离、批量关联和计数；独立编译完整GatewayServer+gateway_demo及exact restoredExecutor、新Cache、finiteidleConnection覆盖cached对象，避免rejectedfair旧library，原SDKlibs/ELF不覆盖、不CMakebuildMessage84candidate。

提交前上述新测试及完整Gateway编译NOT_RUN，仍须现场会话回归和匹配150/s压力评估。静态本地准备attempt1使用不存在KickOldConnection作边界而失败，仅已审计header写入；原generator/failure/部分headerSHA保留，按真实PushOfflineMessages边界修正并从preimage确定性生成，未发生guest写/编译/运行失败。单组件ABBA此前nativeCPU-7.45/8.75%、Rediswhole-0.237/0.240cores是真实受控收益，freshness P99升至约8ms；不是privateACK/全功能/50k容量验收。本轮无部署、19身份/config/health不变，Message原ddc/b24/c119 User38 MCPbc85、SQLdurability1/1/1/0/0保持。所有apps保留，不清理删文件，不改VM/security/NIC，不重复已否定盲调参。所有raw/失败/代码Git保存，继续验证及迭代。


### 2026-10-05：真实合批生命周期224与完整网关构建通过，准备镜像/端到端验证

38f41a9 actualmaintenance/cache pool1/4各112合224全部PASS：pending覆盖inflight/input/ready且上限8，overload与shutdown立即拒绝，beforeIO取消/deadline/probeexception、afterIO取消/callbackexception隔离，每accepted恰一终态；四heldworker受控full16两group结果关联和owner保留，typed mismatch10/refreshed26/36completed，backendthrow与cardinality不符不影响后续任务。fullgroup测试collector500ms只是受控排队，不是性能测量，正常capacity用default5ms。完整Gateway/demo自有编译链接PASS，ELF c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f、原cached库/ELF/19configs完全保持；own33fcExecutor/deefCache/506Connection前置，未build/deployMessage84。

v30导出在生成archive/audit前失败：fullGateway link.map33533003bytes超过generic20MiB；ELF33951840正确先排除，不涉及源码/224检查/构建失败。全map/raw仍保存。新v31仅已知该自有map路径允许64MiB，其余nonELF仍20MiB，密钥精确RAM扫描及SHA保留；v30归档不存在，不能声称已下载。失败及解决保留本地audit/online-maintenance-export-v30-failure-before-v31.json。

本轮10路径仅工具/文档预审计：封装only38Gateway ELF到原1d8 base新tag、UID1000 no-net/no-cap只读loader/missingconfig控制；先原10k/150/s60秒9000 baseline，候选部署仅两GW增加exactbatch_enable=1且保存privateinspect/log、原env/rollback指令，其他17/config/worker16/recovery/fanout/SQLdurability1/1/1/0/0保持。回滚先capture候选log，capture失败仍回滚，恢复原1d8及flag缺省；保留全部候选/rows/log，不prune/delete。原matched压力工具精确改GW身份，新run batch150A1/B1/B2/A2，worker5d6bd183及原Messagec119固定，activewindow readonlydigest/wholecgroup+gueststat/PSI、all9000完整attempt/positive/wire/SQL/HB相等gates、每个FAIL保存。源helpers提交前全NOT_RUN。

现场已有功能链用新519880/882/884/886四个synthetic身份，执行前确认socialpair为空，正常friend/private/read/group/file+wire/durable/checksum，仅样本回归非TLS/MCP/AI/offline/fault/50k全验收。后续TTL缺失/旧连接下线/跨GW替换会话测试另审计。3secI/Oidle非wholecmd/全局停机绝对deadline，不从组件少命令推断端到端极致。若端到端不能重复改善则还原并准确分析剩余瓶颈；不盲重复callback/32worker/groupcommitdelay/skiphealthyPing。所有apps/game/Python保留，不清内存删文件/改VM/security/NIC，源迭代Git和原raw完整留存。全10k50k所有功能极致尚未达到，持续推进。


### 2026-10-05：原10k150基线P99仍180.5ms，真实会话交叉验证候选已健康

5dc3cb9已提交镜像/控制工具；sealedimage a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9仅38f41Gateway ELF c289到原1d8base，UID1000 loader及expectedmissingconfigexit1通过，全部旧SDK目标保持。v31 source/lifecycle/fullbuild54文件/55条目 SHA00a187fd2dd95d1dcf75e912b733f322d6ef14e274f71804b35612df19182e09全54本地SHA通过，完整map保存，v30大小失败未丢证据。

baseline batch150A1实际完成、privateexit2/FAIL：10klogin、9000attempt/positive/wire/receiverACK及SQLconfirmed全部正确，无negative/skip/late/disconnect，HB82612/82612；P99180.5、scheduledP99181.7、max346.497ms，active8990/60=149.8333/s，仅延迟gates失败。不能把completed状态当性能PASS；两个activewindow readonlydigest/19wholecgroup/gueststat/PSI等raw已保留。候选尚未测性能。

双Gateway候选部署a8已健康，gateway-a ecb08556be0f75ec39fd4b6e2fc1104479b3bc37d21db7455257acc9c136ed56、gateway-b a7fedf6c9293de88f7da6cf7b3b63a22f588b76e818e9b491a85d4e78d1a925f；其他17精确身份/启动/privateconfig不变，env只新增batchEnable1，原worker16/recovery/fanout/pools/SQLdurability保持，privateoldlogs/inspect和原1d8rollback已保存。此轮source提交不再部署或改productionCPP。

7路径预审计新增真实twoGW session actor，existing synthetic519890/519892 readonlySQL精确验证username/status及在线key初始missing；仅自有会话正常login/HB/close和全recordCAS EXPIRE1（每次mutation前独立audit）强制过期，privateRedis密码onlyRAM、无DEL/SQL直接写/其他key/全局设置。验证missingrestore、同GW替换/旧EOF、新owner不受旧cleanup影响、跨GW接管后旧HB/旧close不覆盖新owner、新ownerclose/inflightmaintenance和expiredownsession32HBclose不复活；主动关闭burst只报告clientwrites不伪称serveraccepted，offline watch10samples/1sec明确限制，不估算竞态频率或全负载P99。Probe原始body/owner/PTTL/wire/seq以及失败完整保存，90sec ownPID/starttime/argv/PGID守护，无其他应用停机/清理。

提交前liveactor/现成功能链/候选150性能NOT_RUN，继续功能门控后再测匹配负载。新v32仅明确完成/失败自有stages，冻结所有raw与源工具；既有Messageddc/b24/c119、User38、MCPbc85保持。3secI/Oidle非absolutewholecmd/globalshutdown proof；全10k50k所有功能极致未达到，持续凭端到端CPU/SQL/latency证据迭代，不盲重复旧被否定方向。所有apps/game/Python保留，无任意删除、VM/security/NIC变化。


### 2026-10-05：真实双GW36检查PASS，功能链0操作fixture碰撞修正为只读选择新pair

daf296c真实online-maintenance-session-control实际36检查PASS、18个Pong样本：ownTTL CAS过期后missing恢复原conn，sameGW替换旧EOF/newconn保护，crossGW接管后旧8HB及旧close保留新owner，newownerHB后close最终missing/10samples1sec不重建，第二ownUID TTL过期后32clientHBwrite立即close最终missing/10samples。主动断开burst没有伪称serveraccepted，实际2UID功能交叉样本不等于竞态频率或完整压力P99。全部19/config完全保持，无其他账号缓存/SQL写/全局配置操作。

batchfunc1原功能链在built-inpreflight因已有socialpair记录而停止，operations_completed=0，无login/friend/group/file变更，原失败result/trace/log完整保留。这是fixture选择碰撞，不是业务实现失败或性能退化。只读在reserved519800..519950范围检查exactusername/status、所有双向relation/request和RedisEXISTS0，验证新pairs519800/519803、519801/519804，未执行SQL/Redis写或重置。7路径audit新增freshbatchfunc2/functional attempt2包装，原cross_feature_actor不改且执行前再次同样fixture验证；仅正常公共API创建own新记录/explicitown featuredisband/cancel，所有history/files保持。ownactor300sec PID/starttime/argv/PGID保护，fileRPC子进程继承自有group，最后runtime/config核对。候选38/a8两GW保持，其他17未变化。

新v33明确保存batchfunc1的0操作preflight失败、36真实session与attempt2所有raw、原10k150A1 P99180.5/scheduled181.7FAIL和后来候选控制/rollback有则冻结；不覆盖任何旧结果。提交前attempt2和candidate150均NOT_RUN，功能门控通过后再匹配测量；极致所有功能10k50k未达，不把completedstate/小样本PASS当容量验收。Messageddc/b24/c119、User38/MCPbc85、SQLdurability1/1/1/0/0和所有apps/game/Python保持，无清理/删除/安全/VM/NIC更改，持续准确分析和Git迭代。


### 2026-10-05：候选10k150 ACK P99首次180.5→104.4，继续同窗瓶颈核对

30bfa3d已提交fixture碰撞修正与只读选择；freshbatchfunc2真实49operations/35assertions全部PASS，4actors519800/803/801/804各HB1/1，friendaccept/reject/private幂等wire/history/read/group权限/fanout/file真实bytes/checksum均通过；原batchfunc1零操作已有关系preflightFAIL完整保存，36真实twoGW sessionPASS保持。仍是功能样本，不是全功能50k容量。

actualcandidate batch150B1 completed/privateexit2/FAIL，原同10k15060秒9000负载全部login/attempt/positive/wire/receiverACK/SQLconfirmed，negative/skip/late/disconnect0、HB82196/82196；P99104.4、scheduled106.0、max178.393ms、active8991/60=149.85/s、within100ms8884/9000。baselineA1相同完整gates、HB82612/82612、P99180.5/scheduled181.7/max346.497/within100ms8155，首次ACK尾延迟降42.16%，明显但仍不达100门槛和极致目标；没有把completed当PASS。候选B2/A2尚未执行，不能用单次sharedhost差异证明可重复极致。

本轮8路径预审计只新增分析/保全工具，产品source/CMake/运行19/privateconfig不改。按SHA冻结2窗口19wholecgroup CPU、gueststat/CPU/IO/memoryPSI、SQLdigest ps差分/count→mean-ms/1e9及captureduration/activewindow界限；原100us histogram/完整9000 send→ACK/wire MID/CID/UID/seq严格关联，signedwire-minusACK允许先投递再senderACK的合法负值、不筛掉。缺失/失败窗口明示unqualified，不假造9000分位；mean不是P99或exclusiveRPC CPU，内核waitshare不是CPU，wholeRedis/MySQL包括background，guest压力不独占指向IO/调度。

新增readonly阶段日志采集：baselineGW来自部署前privatefullLog，candidateGW与原Message按A1UTC→当前区间保存privatefullLogs，再仅提取matchedpositiveMID允许numericfields；chatUID/seq/epoch>0、peer内部UID/epoch0必须按实际上下文判别，persistence字段mid/from正确。Gateway>=100ms/rate8及repo>=10ms/rate8偏置样本不当全体P99，不拿不同人群均值相减声称瓶颈占比，零日志不意味着零工作。分析结果提交前NOT_RUN，不凭希望宣称CPU或commit收益。

候选日后回滚时原before-log捕获会丢SIGTERM后drain统计，本轮添加独立wrapper预审计2个ownDocker logs --follow进程到runtime-private再调用原已审计2GWrollback；通过stream保留oldcontainer停止/移除期间真实batchaccepted/terminals/pending/maxbatch日志。日志失败不阻止恢复，只有ownPID/starttime/argv/PGID验证后关闭followers，缺失line只算未证实；原rollback按精确1d8/flag缺省/worker16/other17/config保护，source旧helper不改且所有raw/失败/rows/files保留。v34显式扩展analysis/source/phases/drainrollback和未来B2/A2完整/失败记录，不覆盖旧归档。

当前a8/38候选两GW healthy，Message原ddc/b24/c119 User38 MCPbc85 SQLdurability1/1/1/0/0保持，所有apps/game/Python保留，不删文件清内存/改VM/security/NIC。先分析同窗机制与剩余长尾，再根据首次明显改善决定B2/A2复制验证或下轮具体修复，不能盲重复加worker/callback/groupcommitdelay/跳healthyPing。全10k50k所有功能极致未达到，继续准确分析并Git迭代。


### 2026-10-05：完整10k150 ABBA确认维护批处理收益，仍继续长尾优化

b911b2b的分析/私有日志保全/停机持续stream工具实际执行成功。四轮相同10k登录、150/s×60s=9000原负载，Message原ddc/b24/c119、原worker5d6bd、durability1/1/1/0/0、other17/config/apps保持。每轮9000发送/正ACK/实际wire/receiverACK/SQLconfirmed完整；negative/skip/late/disconnect0，HB82612/82196/82136/81481分别精确相等。counter两快照均在60秒activewindow，完整ledger严格UID/CID/MID/seq关联，未丢弃合法wire先于senderACK负差。

|窗口|ACK P99 ms|实际收取 P99 ms|send→ACK mean ms|Redis核|两GW核|19容器核|CPU PSI some %|
|---|---:|---:|---:|---:|---:|---:|---:|
|原版 A1|180.5|214.744504|50.730026|0.539518|1.370194|5.152189|55.254434|
|候选 B1|104.4|123.064594|38.105161|0.371591|1.234038|4.918230|52.355852|
|候选 B2|110.0|127.127925|36.439514|0.362574|1.231716|4.893547|51.058919|
|原版 A2|153.7|180.573642|44.439333|0.541070|1.343698|5.088737|53.711161|

候选两轮ACK P99均低于两轮原版；对应A1→B1下降42.16%、A2→B2下降28.43%，原版自身存在时段波动，不能将单次42%宣传为全负载稳定收益或对四个P99取均值当总体P99。实际收取同步改善，维护Redis/网关CPU下降重复出现；MessageCPU0.8651/0.8883/0.8803/0.8516，MySQL1.3594/1.4041/1.3953/1.3478未下降，wholecgroup含background不能归为单RPCexclusiveCPU。SQLCOMMIT均值5.4849/4.8350/4.6189/5.2173ms，不是commitP99；guestCPU busy81.53/79.23/78.99/80.58%、约290–296fork/thread每秒以及高CPU排队仍存在；memoryPSI近0，不能把本轮归为缺内存、也不删除应用/缓存。

全部四轮privateexit2/FAIL，100ms两门槛未通过；scheduled P99181.7/106.0/111.6/155.9。仅固定plaintext私聊场景，不是所有功能20k50k/AI/TLS/离线/故障/长稳PASS。真实session36checks/18Pong和functional49operations35assertions保持PASS，原fixture碰撞零操作FAIL保留。已验证源38候选可据真实ABBA条件重新保留，helper要求maxcandidateP99<minbaselineP99、完整所有nonlatencygates、real功能/会话/两drain证据；本sourcecommit仍不改变运行，后续独立预审计精确a8/c289复用，仅2GWs且other17/config/env不变（仅batchflag1）。

两候选GW退出drain实际accepted=terminals分别82179/82207，pending0/maxbatch16/peak23与32/Rediserror0/overload0；A completed82179，B completed82168+cancelIO24+cancelCallback15=82207。batchcalls21748/21741、batchitems82179/82183，合并164362items/43489calls≈3.78items/call，是候选存活期含真实session/functional与两容量轮的统计，不能充作某个60秒窗口的callcount。Missing1/16及Mismatch8/0包含真实过期/takeover测试，不是消息丢失。恢复原GW1d8健康，新CID b1f7b4/913a624，other17/privateconfigs精确保持；候选完整私有日志已在恢复前留存，停机stream全部验证，避免了旧日志捕获缺失。

现有Gateway慢样本>=100ms/rate8与repo>=10ms/rate8是不同偏置人群。A1 chat249/repo488只共享MID23；B1 chat29/repo488交集0；B2 chat47/repo486交集2。Gateway慢样本persistRPC均值79.838/91.007/79.832ms，仅说明已抓到的慢请求中该段突出，不能减去不同仓储人群18.765/16.297/15.433ms而宣称transport占比，更不能用waitshare当CPU。新增strict sameMID/UID/seq/绝对时间配对helper，按每条send→repo start→repo end→ACK分解并检查容差；rpc-minus-repo明确包括应用、响应、传输、调度等，不是纯gRPCCPU。交集不足限制本轮因果分解，下一步针对性补证据或隔离机制，而非盲worker/callback/commitdelay矩阵。配对helper尚NOT_RUN，在新commit后执行并另保存实际结果。

v34实际已导出294文件，SHA218b982f8b164f49fa95ff934c2b646462075556f22eedfb09c3e667a03a0f64，完整四轮raw和failedfixture/functional/source/phase/drain恢复在内；排除privateconfig/env/fullLogs/ELF并RAM扫描，local下载/逐SHA解包待执行，不能提前宣称本机验证。v35新增本source/配对/有效候选保留stage的显式完整或失败守卫，旧归档保持。持续迭代目标未达，不以组件PASS或helpercompleted替代业务延迟验收。


### 2026-10-05：保留ABBA验证候选，隔离验证真正CompletionQueue而非重复callback路线

163d86d提交实际完整ABBA与仍FAIL边界后，strictsameMID配对实际完成：A1仅23交集，ACKmean147.960ms=repo前71.801+repo49.301+repo后26.858；RPC75.151中非repo25.850ms（应用/响应/传输/调度混合，不是pureCPU）。B1交集0，B2仅2，ACK124.811=54.247+54.002+16.563，RPC84.696/非repo30.694、commit15.533。全部从同一条消息直接算出，不减不同人群均值；选中的极少slowbiased请求不能代表9000总体或单一根因。候选retainedstage实际a8/c289 compiled38两GW健康，CID3f19b66/a4159ac，仅batchEnable1差异，other17/config原样。v34本机295entries/294SHA全部验证通过，旧rawfailure/重叠前后/停机证据完整保全。

本轮7路径先审计，仅新增isolatedcompletionqueueprobe工具/文档，不改产品CPP/CMake/任何运行ELF，Messageguarded84仍未部署。根据保留的原Sync/Callback负收益及guest约290–296fork/thread每秒，提出待检验机制：真正AsyncService+CompletionQueue能否比Sync减少框架开销并降低尾延迟。参考官方 https://grpc.io/docs/languages/cpp/async/ ，2CQpollers只处理accept/finish标签、16固定worker做相同20ms睡眠+128byteecho，所有syntheticindex/字段逐条核对。每call双标签生命周期保持到Finishcompletion；未绑定accept的ok=false回收；serverShutdown→workerdrain→cqShutdown→pollerjoin，normalcaseaccepted/workercompleted/finished/对象回收账目严格核对。队列512是inflight+queued总数，不改deadlines/重试/SQL策略。

同既有原型16clients/300/s/20s/6000×ABBA、Sync1CQ MIN1/MAX2精确一致，127.0.0.1:0自分配端口、cached生成proto/grpc库、一台owncompiler，新stage隔离object/ELF，不链接任何productimplementationarchives/no真实DBRedisAI/用户，all19/配置保持且>2GiB/noactivecapacity。输出CPUuser/system/上下文切换和每条caller/handler/handlerCPU/extra/lateness/raw，全失败保留；不使用traced性能或把线程减少当性能结论。CPU是同进程client+server、额外wall含固定workerqueue，不等于真实MessageexclusiveCPU。normal机制不证明production取消/过载/SQLdeadline/故障/持久化/全关停契约，AsyncContext无done通知时不调用IsCancelled；产品路径仍原Sync保持。

CQ机制尚NOT_RUN，只在实测CPU/延迟同时值得时才考虑生产具体实现，并需要领域/生命周期/同负载端到端复核。若无改善停止这个方向但继续其他准确瓶颈，不重复盲加worker或callback路线。v36显式保存本source/probe完整或失败及上一retained/join记录，原归档不覆盖，私有数据/ELF排除和RAMsecret/SHA守卫保持。全功能10k50k极致仍未达到，继续迭代。


### 2026-10-05：真CQ+blockingworker负收益已记录，先验证SQL多往返机制

3469f73真实CQ四轮6000各/24ksynthetics全部正确。Sync A1/A2 CPU0.517045/0.500381核、callerP9922.828491/22.707419ms、matchedextraP992.460499/2.381103ms，423/438handlerTIDs；CQ B1/B2 CPU0.568523/0.581996核、callerP9922.850198/22.954109ms、extra2.543100/2.598802ms，16handlerTIDs，voluntaryctx82863/82905 vs64718/64613。CQ每轮accepted=submitted=completed=finish6000、allocated=deleted6001/oneunboundacceptcanceled/live/pending0/peak7，无漏对象/反例/异常，但CPU更高且尾延迟未改善。拒绝这个CQ+blockingworker方案进入产品，不把固定线程少当性能、不泛化所有异步策略无效。所有raw源码/编译/账目/失败限制保全；v36已352文件SHAe3c1e0c249c3d9466f5330d94677cce0b741e75cf7b920322e1f45f5ced08982，本机下载尚NOT_RUN。

只读SQLpreflight实际8.0.40，durable1/1/1/0/0，原连接已经CLIENT_MULTI_STATEMENTS，原两表无trigger，private有两FK→im_users；原Query只读首结果而Execute会消费多结果，不能直接用原Query拼接SQL而遗留结果/忽略错误。官方多结果契约 https://dev.mysql.com/doc/c-api/8.0/en/c-api-multiple-queries.html 要求逐个结果消费和区分next_result=-1/0/>0。本轮8路径先审计，只新增native成本probe和记录，不改产品driver/API/CMake/运行代码。提出待验证机制：PING+precheck+BEGIN维持，同INSERT/identitySELECT/outboxINSERTSELECT/COMMIT四条SQL一次packet vs四次（含PING每条新消息7→4native请求），无skipPING/commitdelay/弱化ACK。

在单独两张absent且fixed命名codex_sql_rtt_messages_20261005/codex_sql_rtt_outbox_20261005上做真实durableInnoDB提交；原表LIKE复制列/索引/check/charset，LIKE遗漏FK所以只在ownmessage表加两个ownnamed同RESTRICT/CASCADE FK→原已验证synthetic700001..710000用户，原用户/消息/outbox/schema数据不改、normalprojector/relay不会读own表。DDL之前单独审计所有语句/源SHA/原schema/index/无trigger/own名不存在/10000用户名status1，然后3ownDDL分别再写audit；任何失败保留partial，不DROP/DELETE/TRUNCATE/重建。配置密码只读RAM，不写配置/环境/密钥。

16专属native连接同原clientflag/utf8mb4/5sI/O，300/s20s6000×ABBA，24k own消息+24k ownoutbox全部提交保存、全字段/JSON/uniqueMID/CID/计数/事务结束验证，所有raw单条CPU/阶段/迟发及SQLerrno保全。Native-Docker线路不是product路线，processCPU是caller，wholeMySQLcgroup含background/连接setup/只读audit，不作exclusiveCPU推断。业务完整性、领域EventCodec、并发幂等/注入错误/identitytampering/提交不确定恢复还需要产品后续特定回归；component正常成功不等于生产协议/全10k50k。完整比较前NOT_RUN，无preset收益结论。all19/config/currenta8candidate/noactivecapacity/>2GiB/>1GiBdisk守卫，原索引/durability前后保护；一编译器ownELF/noSDK，无任何production archives链接。v37显式source/preflight/probe所有complete或failed/ownDDL记录，原档案保持。继续针对实测瓶颈，所有功能极致尚未达到。


### 2026-10-05：SQL往返实测收益及提交前校验边界

becd组件正常ABBA各6000真实durable own表事务。Single A1/A2 caller mean6.993567/6.965908ms、P9912.320544/12.148644ms、client CPU0.471217/0.467212核、MySQL wholecgroup1.213829/1.212815核；fourSQL batch B1/B2 mean6.014814/6.169501ms、P9910.398923/11.185632ms、client0.411941/0.424788核、MySQL1.048054/1.069916核。PING/precheck/BEGIN保持，native7→4，对照一致24k消息和24koutbox保留，failure0；MySQL cgroup包含背景/连接setup，native线路非RPC，不外推全负载P99。v37实际426文件已封存SHAa769f17f9bc17ec1c84dd0cac56ca1a2c563a2133df9b7a51ff6ac2422b8e66d。

审查发现上一成本probe在combinedCOMMIT后才校验C++记录，正常路径虽正确但不能作为产品API：错误记录必须在outbox/COMMIT前拒绝。拒绝直接移植四SQLbatch，原源码/结果保留。新7路径审计只新增safe组件；同6SQL/PING/precheck两控制均先校验全部record和insert_id再单独outboxINSERT/COMMIT。只把START TRANSACTION+INSERT+identitySELECT3语句单包，native7→5。使用既有两own表，schema/index/FK/24k旧行和无新CIDprefix先核对，不DDL/delete/原消息写。4固定轮300/s20s6000共追加24kown消息/outbox，记录新总数48k，不覆盖旧行。两negative fixture（identity projection故意错recipient、中间SQL1054）均需校验拒绝/rollback后0行/同connection SELECT1可用，测量前counter重置。组件仍不证明全生产EventCodec/幂等竞争/提交不确定/故障恢复，后续产品需逐项回归；本轮未执行前不预设收益。原durability1/1/1/0/0、所有19容器/config/其它应用保持。代码/每条raw/失败/审计新阶段独立Git及v38保全。


### 2026-10-05：安全事务前半段实测通过，产品默认OFF候选

6cb safe组件ABBA各6000：singleA1/A2 mean7.466654/7.338825ms，P9914.603638/13.306077ms，clientCPU0.479838/0.482842，wholeMySQL1.268399/1.285254；safeB1/B2 mean7.101917/6.846974ms，P9912.756773/12.587563ms，client0.464834/0.454095，wholeMySQL1.227017/1.187353。两候选尾延迟及CPU均低于两个控制，但不外推native组件为端到端收益。新增24k消息+24kownoutbox全配对/零失败，合计48k/48k保留。各轮两negative：错误recipient投影及第三SQL1054→提交前拒绝/rollback0行/同连接SELECT1可用，八次PASS；中间失败不是完整故障或提交不确定认证。

新的14路径源审计实现默认OFF TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE=1：只合并START TRANSACTION+INSERT+完整记录SELECT，完整C++原BuildMessagesFromResult与same_identity验证后才原领域EventCodec/outbox/COMMIT。PING/precheck/唯一键竞争/释放lease后recovery、outbox失败rollback、commit outcome ambiguous recovery保持原逻辑。专用driver逐一消费恰3results，记录BEGIN/INSERTpartialack；server statementerror留可回滚事务，client/未知protocol关闭自身无COMMIT session，绝不假成功或盲重试。新增combined phase独立标记，不把组合时间归因单独INSERT。原路径默认OFF保留。

准确核对Git发现84拒绝的guarded ReceiverConfirm实际在Adapter/ApplicationService/Port/Adapter.h；MessageRepository.cpp本身与ddc无差异。先从ddc原Git对象SHA核对恢复这四文件，再仅给Adapter加新persist分支，保留旧实验完整Git历史。未将错误文件名当作恢复依据。所有19运行容器/私有config未改，产品新源码尚未部署或验收；独立真实SQL回归/transportfault/端到端控制后才决定是否保留。原MySQL8.0.40/1/1/1/0/0/index13维持，游戏及其它应用保留。


### 2026-10-05：新候选独立构建与真实SQL回归流程，执行前审计

ed90提交产品默认OFF候选及真实fault测试，运行仍a8Gateway/b24Message不变。新增7路径工具审计。独立构建只新stage的四own对象Driver/MessageRepository/Adapter/ApplicationService和8个测试ELF，用真实缓存flags，own符号在SDK/产品archives之前。借用全部archive逐个SHA前后相等，既有cache/CMake/产品ELF完全不写。原trace/应用/CrashWindow/receiverACK/chatACK五unit，加真实outbox集成/unread聚合/新增batchfault；单编译器180s守卫及unit60s真实SQL90s，所有PID/start/argv/PGID记录且只停本任务子进程。

四个absent ownschema codex_private_batch_20261005_off_p1/off_p4/on_p1/on_p4，DDL之前原SHOWCREATE元数据/FK仅指各schema ownUsers、两own用户10001/10002、计划SHA审计；无生产rowcopy/grant/index/DDL配置变化。实际领域event/并发幂等4thread/pool1回收/outbox失败回滚/接收确认和read对照flag0/1。Pool1附加新driver partialACK、1054/1062、额外result关闭session、包含引号/分号/中文content、正确解析但错type和malformedtime必须outbox之前拒绝。session TEMP shadow只在ownschema，关闭自身连接自然释放；没有DROP/DELETE/TRUNCATE/reset指令，所有durable成功测试行和schema保留。private root配置0600只存runtime-private，不显示/导出。COMMIT真实响应丢失专用case已有但需独立只拦自身连接的transportfault审计后执行，当前NOT_RUN，不能称提交不确定认证通过。本轮执行前BUILD/SQL均NOT_RUN，不伪造结果。v39新增所有stage complete或failed、原源码基线及sourceaudit显式保全。全功能10k50k未验收，端到端比较尚待正确性回归。
