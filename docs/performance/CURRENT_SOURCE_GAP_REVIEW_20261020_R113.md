# 源码差距、优化取舍与真实接入缺陷

最新入口：[逐函数源码审计及新鲜完整对照](SOURCE_GAP_AUDIT_20261020_R132.md)。native组合10000连接/700条每秒/60s，42000条全部ACK/投递及存储、broker、Redis核对通过：ACK P99 18.8822ms、VM 5.441忙核。新固定Peer R24同负载42000条及全量历史通过：12.9549ms、0.952忙核。尾延迟仍1.458倍、CPU仍5.718倍，没有容量或已追平结论。下文保留历史轮次状态，不能把旧“尚未接入真实Message”等描述当作当前状态。

本页回答“具体源码差异在哪里、为什么仍未追平”。固定 WuKongIM 提交为 `a888f89533d0e7d1b2030e06504ca97f1ad891d4`，TinyIMX 复盘基线为 `a222c8fd2d90b40ce308aea58c3c625666284900`。运行程序必须另看每轮 ELF 清单：R12 是此前完整性能轮；本地存储是新的可选架构候选，不能拿其组件测试替代完整性能。

## 当前完整系统的实测差距

既有相同工作点：10000 认证连接、全部参与、128 B、700 条/s、60 s。Tiny R12 四轮 OFF/ON/ON/OFF 共 168000 条成功，ACK P99 为 42.7668 / 38.5450 / 41.1990 / 44.5990 ms，整机平均忙核 5.87–5.93。固定 Peer 独立 QPC 参考轮为 ACK P99 12.5513 ms、投递 P99 13.504829 ms、平均忙核 0.951743。前者见 [完整 ABBA](../../benchmark/results/peer-20261017/PERF102-full-abba-comparison-r9/result.json)。早期 500/s 的 4.8371 ms 属于另一工作点。

这些是已有结果，未证明硬件容量上限，也不是本轮候选的测量。Host 内存、采样换页和两系统语义差异保留在各轮报告中；不能把所有差值都归因于某一个函数。

## 一项项对照到函数

|问题 ID|固定 WuKongIM 的实际路径|TinyIMX 的实际差异|改动与证据决定|
|---|---|---|---|
|PERF-099：权限|`PermissionService.requestAllowSend` 在本地所有者调用 `allowSend`；后者查本地 Store 黑/白名单，远端所有者才 RPC。|原 `GatewayServer` 每消息调用 Social RPC，之后做双向好友关系 SQL。R11 可选本地端口省掉 RPC，但每条仍查权威 SQL，尚不是 Peer 的本地 KV。|完整同构建 ABBA 两对 ACK P99 降低 18.3749% / 5.2435%；这项有效但不足以追平。保留逐条撤权生效，未用无失效缓存换成绩。|
|PERF-055 / 096 / 102：新消息持久化|`wkdb.AppendMessages` 写消息键、序号等，`CommitWait` 同步完成后返回；分片 BatchDB 可以合并已到达请求并 `pebble.Sync`。|原 `MessageRepositoryAdapter::PersistPrivateMessage` 需要连接租借/PING、CID 预查、事务、消息写入/完整回读、Outbox 写入、COMMIT。R12 只把 Outbox INSERT 与 COMMIT 合进一次客户端协议请求，语句及原子性仍保留。|R12 完整 ABBA 两对 ACK P99 仅改善 9.87% / 7.62%。它减少往返，没有消除事务和后台工作。新的本地 WriteBatch 候选才改变存储边界，尚待完整性能轮。|
|PERF-054：接收 ACK|普通 `recvack` 移除连接重试项；SyncOnce/master 另有异步会话位置更新。|原 Tiny 普通 ACK 还要把消息状态持久更新到 MySQL；候选改为本地同步 WriteBatch，仍比 Peer 的普通内存移除多一条持久状态路径。|旧 5 ms 收集合批减少约 81.33% UPDATE，但完整 ACK P99 37.0100→36.8175 ms，未证明稳定收益，不把 SQL 条数减少宣称为端到端提速。|
|PERF-062 / 083：后台派生|存储后推进进程内频道分发事件，不等价于 Tiny 的 SQL Outbox + 外部 MQ + SQL 未读真值读取。|Tiny 另有 Outbox 领取/续租/发布回写、broker/proxy、未读快照与 Redis 投影；这些虽不全在发送 ACK 的直接等待内，仍争用同 VM 的 CPU/数据库。|旧观测 MySQL 2.263 核、broker/proxy 1.191 核，占当时整机 6.104 核约 56.59%。这是资源占用，不是逐消息时延占比。暂停后台产生欠账的成绩不接受。新候选改本地原子事件+有界发布器+owner 快照 RPC，保留真实 MQ/Redis。|
|PERF-087 / 093 / 094：队列与线程|频道 processing 约束其同步 `onSend(permission→persist→sendack)`；并非所有阶段天然并行。存储分片批次与频道事件批次是不同层级。|Tiny 用户对 FIFO 覆盖更长 RPC/存储链，hash stripe 还可能让无关键等待同一前驱。释放工作线程不会自动解除逻辑 FIFO。|旧 ready4 把 Gateway 共同样本排队均值 4.4212→9.0402 ms；尾延迟变差，默认关。精确键首对 44.5409→45.0597 ms，也未证明收益。不能再把“异步/合批”本身写成优化成果。|
|PERF-103：发送 ACK 边界|`onSend` 在权限/持久化之后生成 sendack，分发由事件推进。|Tiny 最终 ACK 还包含未读快照和接收提交/状态字段。|未提前发送简化 ACK。Peer 投递延迟也更低，故此差异不能解释全部差距。|
|PERF-105.5：新候选保留成本|Peer CID/消息身份在自身本地模型内处理。|`MySqlLegacyIdentity::Resolve` 每消息仍借 SQL 连接并查询 MySQL `WEIGHT_STRING`，必要时查用户存在性；首次用户引用还需持久 pin。|当前不是纯本地链路。该桥维持旧 `utf8mb4_unicode_ci / PAD SPACE` 幂等语义，不能直接改字节比较；后续应单独测 PING/权重查询/首次 pin，再决定有界缓存或兼容实现，不能先声称消除 SQL。|

对应冻结源码和函数行号全部保存在 [逐文件 SHA/锚点清单](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/result.json)。当前候选源码入口：

- [CID SQL 桥](../../services/message/local/MySqlLegacyIdentity.cpp)、[连接租借与 PING](../../common/db/MySqlConnectionPool.cpp)。
- [本地同步提交、接收确认与索引](../../services/message/local/LocalPrivateStore.cpp)、[真实 Message 组合入口](../../services/message/local/NativeMessageRuntime.cpp)。
- [有界后台发布](../../services/message/local/LocalEventRelay.cpp)、[远程未读快照](../../services/projection/unread/UnreadSnapshotRpc.cpp)。

不能由“Peer 频道执行 99.36% 是单条消息”推断“存储层不存在跨频道合批”。BatchDB 是另一层。既有 BatchDB 静态审计已经确认合并机制，正式负载下真实填充率/同步提交次数仍需对应观测。

## 为什么尚未优化到位

1. 已有完整性能改善主要来自减少 RPC 和 SQL 往返，尚未改变工作量最大的持久化及后台架构；其收益本来就不足以跨过当前差距。
2. 一些调度候选已经出现负结果，应关闭并保留证据，不能不断用同类候选掩盖未解决的核心路径。
3. 本地存储候选已接到实际 Message 和独立 projector，并验证引用、恢复与投影；但此前没经过实际 Gateway 的完整协议客户端。兼容性验证漏项会让“服务 RPC 成功”与“原客户端可用”分离，本轮已经实际检出一例。
4. 当前没有新候选完整同负载 ABBA，因此没有“已经优化到位”的数据依据。后续保留与否仍取决于完整正确性、阶段成本和正式性能。

## 本轮实际暴露的缺陷：历史排序契约

**PERF-114（由 PERF-113 双网关功能门槛检出）**：新 `LocalPrivateStore::ListHistory` 逆向扫描后直接返回降序。原 `MessageApplicationService::ListHistory` 需要“最新窗口、窗口内升序”，以便删掉第一条 lookahead；原 `MessageRpcClient::ListHistory` 还严格拒绝非递增 MID。此前 raw RPC 测试只核对条数，未走该客户端校验，测试中的降序期待本身也写错。

实测 R113：Windows 实际认证、跨网关放置、未登录/伪造/非好友拒绝、12 条有序投递、16 条双向 FIFO、28 个唯一 MID、CID 重用/冲突已通过；历史集合门槛失败，后续 Group/ACK 丢失/会话替换没有执行，整轮为失败。原20服务已恢复，旧容器/快照均保留。[失败门槛](../../benchmark/results/peer-20261017/perf113-native-live-r1/contracts.json)、[恢复与原始日志归档哈希](../../benchmark/results/peer-20261017/perf113-native-live-r1/failure-preservation.json)。

修复：继续反向取最新有限窗口，再按升序返回；新增最新窗口、lookahead 裁剪、排他游标的连续分页验证，并补 raw RPC 升序验证。该项是必要兼容修复，不是性能收益。完整重新构建/普通与 sanitizer、崩溃恢复及新的双网关复验结果以相应 R16/R114 结果文件为准；没有结果前保持待验证。

后续顺序：修复与完整功能门槛 → 同 MID 低采样阶段分解 → 无诊断同构建 SQL/native ABBA 与固定 Peer → 容量、持续稳定性、真实 broker 成功后进度落盘前的故障验证。每轮都保留 SQL CID 桥与额外接收确认语义，不以弱化 ACK、停后台或改分母获取成绩。


## 已执行的闭环与仍未通过的边界

R16普通/ASan+UBSan核心每配置45检查、应用各89、发布器各111、RPC18/155/64及16次实际终止恢复通过。R6实际wire 74检查全部通过；最终只读114条消息均已确认，待投递/发布/隔离均0，数据文件未改变。

R2是验证器漏翻页；R3未发心跳就期待远端关闭。R6明确记录旧连接在心跳前仍可读，发心跳后失效，此为PERF-116开放边界。R6原始收尾断言要求INFO统计行，但未改变的warn配置将它过滤；后续使用更强的只读引擎全量核对，没有捏造运行时published计数。只读工具的API与二进制值解析失败也保存。

补充后台核对发现PERF-117：GroupEventFactory固定旧topic，两条群消息事件在隔离topic发布成功，6条群域控制事件因topic不匹配失败。原业务数据计数、原20运行身份/健康和九保留库保持；没有将失败域事件删除或向原topic补发。当前状态是“私聊完整功能门槛通过、群控制权限通过、群域后台路由待修复”，没有新的完整性能成绩。下一步先按配置传递Group域topic并复验，再进行同MID与正式ABBA。


## R118：群域topic缺口已闭环，当前性能工作转向登记桥

GroupService配置经adapter传入事件工厂WithTopic，默认兼容、事件ID/载荷/幂等键及事务不变。R23普通/sanitizer各58工厂检查、27群事务、49成员变更通过；28项实际Gateway业务检查通过。真实broker19个envelope/topic/tag/key与SQL一致，包含完整新一轮13次群域变更（10个域事件类型）和1条群消息事件；原topic8队列位点不变，旧6失败行保留。群消息仅验证1条对两接收者投递与CID重用，未据此声称群多消息FIFO。

R1时间参数错误、R2观察器boxed Long/Integer比较误报均保留。修复后只重跑只读broker验证，没有重放业务。最后只读原生库完整KV与此前114消息状态逐字节一致，全部confirmed，原20服务、1905总容器、9保留库与binlog保留。

源码进一步确认PERF-121：NativeActorReferences::EnsureActors持有全局actor_mutex调用Pin，锁内执行池租借/PING及引用INSERT；不同冷用户对也串行。它使用独立池租借，并非owner监控连接。该成本尚未实测，不认定为主因。下一项是同MID冷/稳态登记锁、SQL桥与引擎同步写拆分，再按实测决定改动。仍无新的完整A/B或容量成绩。


## R122：实际源码差异、同MID测量和已落地候选

本轮R24默认关闭的分段计时经普通/sanitizer和18次实际进程终止恢复验证。真实Windows客户端1024连接、32闭环工作者，512冷消息+4096暖消息通过持久化ACK、精确投递、512会话FIFO/历史及关闭后4776条native记录核对；原168条逐条不变。该工作点使用短合成文本，与正式10000连接/700条每秒/128B不同，不能拿其客户端耗时对比Peer正式P99。

同MID共同确定性样本冷9、暖28，均为微秒级日志相减后的**均值**，区间包含关系全部通过。

|阶段|冷登记9样本(ms)|暖路径28样本(ms)|边界|
|---|---:|---:|---|
|Gateway业务排队|27.064222|6.254429|发送请求的有序执行器|
|权限|0.759778|1.757857|仍查权威SQL的本地端口|
|持久化RPC整体|29.615444|10.460821|包含下面RPC及native子区间|
|RPC服务内执行|28.710556|6.867643|callback0；不含方法入口前排队|
|native持久化整体|28.649556|6.755536|包含下面各存储子区间|
|身份SQL桥整体|0.526778|0.710071|含租连接/PING与权重/存在性查询|
|首次引用登记整体|27.076333|0.007964|含全局锁、池/PING及INSERT|
|其中登记全局锁等待|25.023667|0.001536|锁内SQL使无关冷用户对也串行|
|其中引用INSERT|1.832667|未执行|首次FK pin，非普通消息INSERT|
|本地引擎同步Write|0.934111|3.939036|不能由每次Write推断每消息独立物理fsync|
|未覆盖native工作|0.109444|2.039786|待拆健康检查、读/编码、通知及调度|

上述区间不能相加成端到端P99。25个所有已输出冷样本的锁均值23.595680ms、52个所有已输出暖样本1.096us，与共同确定性队列不同，不能混用。完整逐MID表和阶段汇总见`PERF122-native-phase-analysis-r1`。

源码优化已落地R25：PinRows不访问缓存；全局actor_mutex只管理有界缓存及in-flight登记，同一actor等待其登记完成，不同冷actor在SQL期间释放全局锁。保留100000 pinned+in-flight上限、FK先于本地提交、失败/不确定SQL回收、owner失效和启动孤立引用清理。开关`TINYIMX_NATIVE_CONCURRENT_ACTOR_PINS_ENABLE`默认OFF。真实SQL行锁合同检验独立用户可在第一对被阻塞期间完成。候选资格与原ELF分开；本节初始候选wire待测状态由下方R125实测更新；仍无完整正式性能收益，不能称为追平。

暖路径仍有明确未完成项：本地同步写、逐消息旧CID排序规则SQL桥和2.040ms未覆盖工作。源码发现admit_events→relay.Healthy→Stats持发布器mutex，Poll也在该锁内扫描/解析/排队；需要计时才能归因。当前callback0并非共享回调队列工作点，不能继续把全部差值归到它；方法入口前的gRPC准入仍未完全测到。

R2验证器错误地期待投递携带CID，导致54条实际业务已完成但客户端轮失败；旧FAIL保留，独立核对确认54条均confirmed。R3按MID/文本及存储JSON真实合同核验，并保留逐请求发起/ACK/投递记录，使用全新冷用户。磁盘清理仅涉及已E逐成员SHA/fsync校验、未被任一保留容器挂载的结束构建文件，全部ELF挂载、数据库、容器、快照和binlog保留。

固定Peer源码的BatchDB一次最多收集100个已就绪batch后pebble.Sync；它没有等价的逐消息MySQL身份桥或FK登记。应借鉴本地数据所有权和就绪聚合，不能直接复制并删除Tiny的CID/外键/可靠性合同。RocksDB本身也有写组机制，后续必须测真实同步次数与批次分布再决定是否需要应用层合批。


### R125同ELF有限闭环对照（更新上述候选wire待测状态）

R25的33项默认OFF真实SQL引用合同、46项ON sanitizer合同及18次进程终止恢复通过。真实SQL行锁验证：一对被外键行锁阻塞时另一对可完成；同actor等待，失败FK后合法重试与重启孤立引用清理通过。

同10个ELF、1024连接、32闭环工作者，每轮512冷消息+4096暖消息，ON/OFF/OFF/ON共18432条本轮协议与native门槛通过；最终23208条native均confirmed，5230个引用，旧记录及原20、全部容器、9库和binlog保持。

|轮次|开关|冷ACK均值(ms)|冷ACK P99(ms)|暖ACK均值(ms)|暖ACK P99(ms)|
|---|---|---:|---:|---:|---:|
|r6|ON|20.3556|39.7875|18.0825|41.7844|
|r7|OFF|55.9977|97.4207|17.7513|36.5908|
|r8|OFF|53.9260|98.0208|17.7252|42.1920|
|r9|ON|20.2603|39.4466|17.4102|37.6936|

两组冷ACK P99降低59.16%/59.76%，全体512请求统计。冷登记共同MID的锁均值由27.030/21.040ms降为1.5/0.5us，样本仅6/8/4/2，不能推广为该阶段完整尾分布。暖P99一组退化14.19%，一组改善10.66%，没有稳定收益，因此候选保留隔离且默认OFF。该锁是native兼容桥中的冷路径问题，不是原SQL基线与Peer全部差距的解释。

本轮是短文本、同并发闭环并保留增长历史/不同冷用户的机制诊断，未固定700/s，未做同状态恢复，也未收集正式整机资源窗口或匹配Peer；当前没有新的完整系统容量或追平结论。当前新事件还需逐项broker/Redis/consumer精确收尾。详细比较见PERF125-cold-pin-diagnostic-abba-r2。

自动审批拒绝权威源码快照删除，所有源码保留；复制到E并独立恢复校验完成。硬链接核对表明213MB逻辑源码仅约13MB可释放，未夸大空间收益。随后清理的130个APT包索引全部先E备份，在APT锁下执行；安装包与仓库配置hash未改变。R4/R5余量失败保留，R6-R9在恢复余量后执行。
