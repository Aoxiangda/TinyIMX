# TinyIMX 与固定 WuKongIM 多路径源码审计

当前结论：**尚未对齐**。本记录把已确认的实现差异、实测和待验证假设分开。开源固定版本与完整原始数据见 `WUKONGIM_IMPLEMENTATION_COMPARISON_20261017.json`。

## AUDIT-01 / PERF-034 客户端排队与执行器粒度

TinyIMX：Reactor之外还有分条带业务执行器；每网关实际16个消息worker，64条带。同条带的阻塞任务可影响其他映射到该条带的用户。

WuKongIM：按用户/频道维护事件队列，以processing约束对应频道串行并提交到调度池。

证据与边界：051 TCP OFF完整MID组：客户端写调用前0.0215ms、网关排队0.1019ms均值，不能解释本轮全部差距；更高负载/长稳已有排队长尾。

下一项验证：容量阶梯逐负载看排队分位数、热点键和实际worker占用，再考虑隔离/异步；不直接堆线程。

源码位置：
- `gateway/business/BusinessExecutor.cpp:488`（SHA-256：`3b707e2e5a63cd7671a92c977ea75fc7fcaad29de1c90e92e41bcb8f9017cb22`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/internal/channel/event/handler.go:62`（SHA-256：`ca8953603b2760896dec7e85eacddd96018bc8328f0104a9118acfe545312e39`）

## AUDIT-02 / PERF-034 每消息权限RPC与查询

TinyIMX：每次私聊同步SocialRPC，再读取关系权限；当前没有用缓存绕过撤销权限。

WuKongIM：本地权限路径可用有界LRU缓存；白名单增删后更新或失效，远端节点仍有RPC。

证据与边界：051 OFF权限独立475组：均值1.3418ms，RPC调用扣handler残差0.6834ms，连接0.2184ms、查询0.2815ms；不同组不当全量占比。

下一项验证：设计带写后更新/失效的权限缓存或批量检查；先测试拉黑、删好友、并发变更、多实例失效、重启与失败关闭。不得直接复制TTL正向缓存。

源码位置：
- `services/rpc/SocialRpcClient.cpp:307`（SHA-256：`dd4f5ebbdafc52ce1dcf8f564482d0b3e4ad74b20e1b7b4cbf75f01e2dabdf62`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/pkg/wkdb/cache_permission.go:23`（SHA-256：`4b7411c54047882b49877beb7b2e19780e68f159f10cd9e7556a90a21879fc75`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/pkg/wkdb/allowlist.go:48`（SHA-256：`cada6156d8f839432dd937b63e065403ec261f53eadf2161c4b936911f90a110`）

## AUDIT-03 / PERF-034 消息持久化提交与额外往返

TinyIMX：一条消息先查CID，BEGIN/INSERT/READ合包，插入Outbox再COMMIT；消息与事件仍为同一事务。

WuKongIM：频道形成一组消息，由存储分片批次CommitWait后进入发送确认流程。不能把NoSync局部选项当成完全无同步持久化。

证据与边界：051 OFF完整403组：持久化RPC4.2388ms，其中仓储3.3997ms、COMMIT2.2197ms；这是嵌套关系，不能累加。此前发送合批/分片候选未证明稳定收益。

下一项验证：独立验证提交调度PERF-053，继续量化批次大小、每批SQL往返、失败回滚及幂等冲突，不能删除Outbox或提前成功ACK。

源码位置：
- `services/message/repository/MessageRepositoryAdapter.cpp:269`（SHA-256：`686345013bae088b37baa50a90fe28d1c2fff759882b564c95efaf5b357a1a74`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/pkg/wkdb/message.go:17`（SHA-256：`92faa1e7b718223dfeb91ffb25f9060a26548b175f85e3aca4251002815300e2`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/internal/channel/handler/event_sendack.go:9`（SHA-256：`eec93e9cf8e8785ab1d4b53f73ccffb684440019184e5757a9bfffcd9b6248a8`）

## AUDIT-04 / PERF-054 普通接收回执的逐条持久化写放大

TinyIMX：正常Pending→Confirmed走带owner/状态/合法性谓词的单条autocommitUPDATE，每条回执都可能触发同步提交。

WuKongIM：普通RECVACK删除内存重试映射/优先队列；SyncOnce命令且主设备才触发异步会话已读位置更新。不是与Tiny逐消息确认落库完全相同的状态语义。

证据与边界：051 OFF约29991次确认UPDATE，诊断均值2.4024ms，累计等待72.05s会交叠，不是ACK链路额外72秒。固定源码已补齐recvack→RemoveRetry→finishMessage。

下一项验证：新054默认关闭：仅给已有有界确认批次增加可选2/5ms收集窗口，保留owner/状态校验和COMMIT后逐调用者结果；先真实SQL异常语义，再同构建0/5ms与直接路径对照。

源码位置：
- `services/message/repository/MessageRepositoryAdapter.cpp:1339`（SHA-256：`686345013bae088b37baa50a90fe28d1c2fff759882b564c95efaf5b357a1a74`）
- `services/message/repository/MessageRepositoryAdapter.cpp:1387`（SHA-256：`686345013bae088b37baa50a90fe28d1c2fff759882b564c95efaf5b357a1a74`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r2/internal/user/handler/event_recvack.go:16`（SHA-256：`164bccfaae7c9d1eeabe78040c220a211d8f2e0a02f6e848a2d49ddcef6613b4`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r2/internal/manager/retry_queue.go:101`（SHA-256：`ce25f53b28a85bfd1719977cd09793e5583831c4a50548a69f58ef7097a56efa`）

## AUDIT-05 / PERF-034 未读投影的逐事件权威重算

TinyIMX：每个MessageCreated事件取SQL同一读视图下的私聊/总数，再更新Redis；有正确性价值，也有每消息查询成本。

WuKongIM：会话按序号/游标及缓存处理，命令回执路径更新会话读取位置，普通回执没有同等SQL计数。

证据与边界：051 OFF约29998次SUM/COUNT查询，诊断平均0.4259ms；不是ACK调用内所有开销，但与前台共用CPU/连接/磁盘。

下一项验证：考虑按接收者合并刷新或批次读取，验证乱序、重复事件、读消息并发、缓存丢失重建、失败重试后SQL/Redis精确一致；不直接把权威重算替成可能重复的INCR。

源码位置：
- `services/projection/unread/UnreadProjectionReader.cpp:56`（SHA-256：`29faaee50f7949d14e78acc7d2fa93a599694162fed05f3312ae8c7dfcad3dc0`）
- `services/projection/unread/UnreadProjector.cpp:66`（SHA-256：`cc9151c6bdc46a2e50efbbe999487c1c97bb80fc95c1c33b87c92321e4018a00`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r2/pkg/wkdb/conversation.go:238`（SHA-256：`0657da12224a3935afaacabaa46d4405f6200053ea386fbcafd5849a4584acb0`）

## AUDIT-06 / PERF-034 跨网关收到消息后的再次权威读取

TinyIMX：接收网关通过MessageService读取完整持久化记录后再核对身份/正文/状态，属于额外RPC与查询。

WuKongIM：已持久化的频道事件带消息内容按tag分发，并非每个本地接收路径都再次调用MySQL消息读取。

证据与边界：额外操作已由源码确认；尚未单独测出对发送ACK的净影响。当前远端发送ACK在可靠接受后先交付，后再提交peer投递，不能把所有接收网关成本直接加到发送ACK。

下一项验证：若考虑可信持久化凭据/不可变记录缓存，先验证伪造MID、正文篡改、owner不符、重复与状态变化，独立量化投递和后台竞争；当前不删除读取。

源码位置：
- `gateway/GatewayServer.cpp:7651`（SHA-256：`46c48e21f18a5a1bd0fb150396b2a296a36f2db7901fbad25dc3e6f06d837bac`）
- `gateway/RemoteDurableAcceptance.h:82`（SHA-256：`e3768f84e8cd103cf49df7c2c6b7ca9a735e8602ccf914d78ffb18a69f01b70c`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/internal/channel/handler/event_distribute.go:47`（SHA-256：`88474e6ef28a0963229ac996e2401228b5f17ed4a9ba8ee5f2699541827d98d0`）

## AUDIT-07 / PERF-052 gRPC连接复用与调度的排除性审计

TinyIMX：GetOrCreateStub已经按target缓存channel/stub并有界驱逐，不能再把每次RPC描述成新建连接。同步依赖会占执行器线程。

WuKongIM：单节点多处为进程内调用，远端才跨节点；调度模型不同。

证据与边界：RPC poller同051反向首对仅约10%尾差，CPU5.1073/5.1105核基本相同；不证明稳定收益，更不等于设置8个业务线程。

下一项验证：保留默认；若继续调整按相同负载重复，另看RPC残差、服务端handler/仓储、CPU和线程数据，不用残差冒充纯网络RTT。

源码位置：
- `services/rpc/MessageRpcClient.cpp:1555`（SHA-256：`4dc229bb0e63048ed2bdd55e7e296b8a35c075e0f8ed4fb85abe40e63aac8de5`）
- `services/message/server/MessageServiceServer.cpp:57`（SHA-256：`c5619b78f5951cf44aea2807fcd6f0a220d11d22a64b7d15a07535d12c16ddbd`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/internal/user/event/poller.go:18`（SHA-256：`b05a9ee0bb6d8328237e0ec3467228f588ab64c2aca33815dd09459a119e62c3`）

## AUDIT-08 / PERF-053 提交日志调度与整机成本

TinyIMX：MySQL事务、接收确认、Outbox领取/回写及MQ后台在同一8核VM；日志线程可能产生调度等待。

WuKongIM：主要为同进程事件处理与本地KV分片提交；资源链路更短，但不能据此直接认定所有差距的单一根因。

证据与边界：同500/s整机忙Tiny5.0668核、peer0.8638核；基准点不是最大容量，不能线性外推QPS。官方SQL日志线程资料仅支持候选假设。

下一项验证：PERF-053保持双1与全部持久化，OFF/ON受控对照；再做更高完整负载与开源同率对照，按修改前后结果取舍。

源码位置：
- `benchmark/peer_compare/mysql_log_writer_trial.py:5`（SHA-256：`218ad45395e68356e2253e49e914831d07f99348266ce0708cf45bfe8be35e1c`）
- `benchmark/results/peer-20261017/peer-code-audit-wukong-r1/pkg/wkdb/message.go:44`（SHA-256：`92faa1e7b718223dfeb91ffb25f9060a26548b175f85e3aca4251002815300e2`）



## R31：私有命名空间验真通过，高负载仍失败，正式OFF资格已修正

独立r16全链路120条正确且SQL/cache/MQ最终一致；r17重新从零历史运行10000连接、1000条/s、60s，全部60000实发，47638完成、12362过载拒绝（20.6033%）。ACK P99为1017.3221ms，首次投递2133.2277ms，发送迟到1.0328ms；全部成功子集接收确认且事件Published，拒绝均无持久化，重复/invalid/timeout为0。MQ窗口328→24488，中段+393.0997/s，未达到稳定容量。Outbox306→141、峰637、中段+2.0473/s；不能仅凭中段回归称Outbox无限增长。

R17继承MessageService持久化采样1，因此明确保留为真实失败诊断点，不能称正式无诊断。最新控制器已将11已知采样键在11实际容器明确置0并在流量前检查；PFS原状态也核验。14个工具冻结/上传SHA一致，正式OFF重复尚未执行。

源码/实测重点：MySQL约2.135忙核、整VM6.7565核；确认UPDATE47629次均值10.5664ms、COMMIT51058次均值9.1859ms，累计等待交叠，不能相加为单请求。未读每事件只有一条同读视图SUM/COUNT，随后Redis快照和同步SDK ACK；4个worker可能受I/O限制。Gateway/Projector实际均写缓存，owner契约与并发一致性仍需审查。固定开源普通RECVACK主要删除内存重试，不能删除Tiny逐消息确认持久化后称语义等价。

补齐负ACK时被跳过的Outbox审计：47638正事件身份/内容/发布全部正确，12362仍留在60000完整失败分母。新scope检查完整，但整个工作点仍FAIL；原报告不覆盖。

维护：622个停止测试副本的配置/日志/镜像文件层完成全SHA和实际载入验证。回收被自动审批以只明确批准四库为由拒绝，尚未执行；已询问精确622授权，九库也仍待另行授权。当前free22029062144B低于15%约56.6MB，无活动压测，原20健康。没有降低门槛或扩展旧授权。准备依次正式OFF基线、仅16→32消息worker、仅4→8投影worker，以完整负载、资源、SQL/cache及后台稳定性决定是否保留。

结构化证据：`benchmark/results/peer-20261017/PERF059-fresh-namespace-point-r31.json`、`PERF041-owned-runtime-smoke-r16-r1.json`、r17的`operating-point-costs.json`/`pipeline-summary-followup-r1.json`、`PERF060-formal-off-harness-r4/manifest.json`及`PERF031-container622-approval-review-r31.json`。所有新增问题包含动作、证据、比较和下一步；最终对齐、最大容量、长稳和故障目标尚未完成。


## R32：完整分布与慢请求拆分、线程复用候选的实际局部验证

R17的60000完整分母离线复核：10000方向用户对的排序key先由实际C++函数验证，再按64条带统计；64均有流量，660–1128次/条带。12362次拒绝全部为global overloaded，热键拒绝0；拒绝最多8条合计15.078%，不支持极少数条带的流量集中解释。真实排序键：私聊发送/已读使用无向用户对，peer转发/接收确认使用MID；它们映射到固定条带后仍可能使无关业务共享串行队列。

修正精确case日志45m时限，补回1472数值行，无截断。242个唯一MID：ACK平均556.442ms，业务上下文到work入口522.635ms，写调用前0.0203ms、权限4.011ms、路由0.934ms、持久化RPC25.437ms、未读0.989ms，剩余wall2.415ms。仓储总20.308ms、获取3.863ms、COMMIT10.946ms嵌套在RPC内。入口等待占此慢样本组均ACK约93.92%，不能外推全部请求；无绝对handoff采样，残差也不是纯网络时间。

整窗口CPU PSI some69.70%、Runnable均45.35、上下文切换约158852/s、内核任务创建1966/s；固定peer参考约0.812%、2.17、26372/s、0.0508/s。Linux processes计数包含线程，不能直接全部归因gRPC。Guest majorfault/换页/内存压力均0，Windows仍有页面读入计数；块IO await也不等于COMMIT。新10秒原栈空闲快照创建约53/s，状态不同，不作受控相减。

三轮消息持久化诊断573/597/592样本用了572/593/589个不同OS TID；实际依赖1.76.0和对应官方线程源码/许可证已保存。新增默认关闭的idle retention32/64（MIN1，CQ1）；旧默认1/2和旧启用4/8保持。MAX_POLLERS不是活跃handler总上限。

新头8环境120检查通过；实际MessageServiceServer局部重编译，4profile256次RPC及84项状态/就绪/关闭检查通过。r1缺生成头编译路径在RPC启动前失败，已保留，r2按实际CMake包含路径修复。固定20ms假工作、32client、1000/s4s的同binary ABBA各4000正确：处理线程TID132/188→32/32，CPU双轮均1.114005→1.0747s约少3.53%，P99双轮均20.5595→20.528ms仅约0.15%。这证明局部复用机制，尚不足接受端到端收益，继续默认OFF；原服务与完整059没有重部署。

差分见`PERF061-retention-candidate-review-r1/candidate.patch`，原始4轮CSV和结果见`PERF061-rpc-retention-mechanism-r1`，取舍见`PERF061-retention-decision-r32.json`。下一步获批空间后先正式OFF基线，再全量061/RPC/SQL/可靠性回归与单变量真实完整负载，依据CPU/线程/内存/队列/RPC/提交/后台决定。额外线程可能加重调度，消息16→32及投影4→8仍须分别测试。容量阶梯、最大持续吞吐、长稳和故障未完成。

本轮没有重试被拒的622对象回收，九待批源库也保留；结束free21958656000B低于15%约127MB，原20身份/健康正常，无活动业务压测。维护批准和写入预算仍是完整压测的前提。局部只读/回环试验不冒充完成对标。
