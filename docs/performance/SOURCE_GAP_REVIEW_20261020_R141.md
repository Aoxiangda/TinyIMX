# 当前 TinyIMX 与固定 WuKongIM 的源码差距及验证

审计对象：WuKongIM `a888f89533d0e7d1b2030e06504ca97f1ad891d4`；TinyIMX 原 SQL 路径与实际运行的 R30 Message / R16 projector / R23 Group 候选。原服务未被候选覆盖。以下区分正式工作点、带诊断的分段观察和待验证假设。

## 1. 尚未追平，差距有明确数字

同一 8 vCPU / 16 GiB Ubuntu、Windows 原生发压、10000 活跃认证连接、700 条/s、60s、128B、空消息历史，各 42000 条全部通过。正式轮关闭阶段日志。

|指标|TinyIMX R30 native 完整候选|固定 WuKongIM|Tiny / Peer|
|---|---:|---:|---:|
|持久化 ACK P99|18.8822 ms|12.9549 ms|1.458|
|计划发送至接收投递 P99|19.063715 ms|14.145358 ms|1.348|
|整台 VM 平均忙核数|5.44115|0.95157|5.718|

证据：[正式比较](../../benchmark/results/peer-20261017/PERF133-fresh-native-peer-comparison-r136-r1/result.json)。这是一对完整工作点，不是容量上限或统计稳定性结论；宿主页读取不同，协议不同，Peer SDK 有载荷加密，两者普通接收 ACK 的持久化语义也不同。旧 R104 的 12.328 ms 等结果继续保留，不能混用旧轮 CPU、当前轮延迟和诊断轮分段拼成同一实验。

**CPU 差距比当前尾延迟差距更大。只把 ACK P99 压低而保留高额后台成本，仍没有完成对标。**

## 2. 对应源码、差异、已做动作、剩余验证

冻结的 16 个源文件和 SHA：[逐文件冻结证明](../../benchmark/results/peer-20261017/PERF034-current-full-source-gap-r135-r1/result.json)。这里的 Peer 链接指向实际固定源码副本；不使用不同版本的 README 成绩代替源码。

|问题 ID|WuKongIM 的具体实现|TinyIMX 当前实现及差异|已经执行的动作与结果|下一项验证|
|---|---|---|---|---|
|PERF-099 权限热路径|[permission.go:216](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/source/peer/internal/service/permission.go#L216)：本地 owner 直接 `allowSend`，远端 owner 才 RPC；权限存储位于进程内本地 DB 链|[LocalPrivateChatPermissionClient.cpp:62](../../services/rpc/LocalPrivateChatPermissionClient.cpp#L62) 已免 Social RPC，但仍向工作池投递、等 future，再逐消息查好友关系 SQL；不是 Peer 的本地权威权限路径|真实撤权和完整流量消融已执行，两方向 ACK P99 观察改善 18.37% / 5.24%；当前候选显式启用本地端口。健康诊断暖样本权限均 0.820 ms|先保留逐条权威权限语义；实现本地权限版本及可靠失效后，比较 SQL 次数、撤权可见性和完整系统结果。不能直接缓存“已允许”|
|PERF-055/096/102/105 存储提交|[message.go:17](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/source/peer/pkg/wkdb/message.go#L17) → `BatchDB`；[wukongdb.go:417](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/source/peer/pkg/wkdb/wukongdb.go#L417) 收集已就绪批次，最多 100 批，`Commit(pebble.Sync)` 后通知；不是定时等待凑批|原 SQL 默认有消息及 Outbox 的事务、身份回读、COMMIT。当前 [LocalPrivateStore.cpp:332](../../services/message/local/LocalPrivateStore.cpp#L332) 已用同步本地原子批次保存消息、CID、事件、未读及投递索引；但应用层每请求写批，仍有外部桥接。RocksDB 引擎内部可能合并写，不能把 Write 调用数当物理 fsync 数|本地候选已实际接入完整系统并通过 42000 条 native、broker 和 Redis 核对；不是只有孤立 KV 测试。正式 ACK P99 18.8822 ms|采实际 WAL 同步/写组；再隔离就绪合批或分片。Peer 实际消息 DB 8 分片，Tiny 1 个 DB，尚不能把差距全部归因分片|
|PERF-105.5 CID/用户兼容 SQL|Peer 使用自身本地键模型，不需要查询 Tiny 的 SQL 排序规则|[MySqlLegacyIdentity.cpp:24](../../services/message/local/MySqlLegacyIdentity.cpp#L24) 每条租连接、可能 PING，`WEIGHT_STRING` 规范 CID，并查双方用户存在性。换本地消息存储后，这条 SQL 桥仍在|健康诊断暖样本身份桥均 0.787 ms：租连接/PING 0.308 ms、查询 0.466 ms。不是纯本地提交|受限本地规范化/版本缓存需要通过 Unicode、大小写、重音、PAD SPACE、用户删除/FK 和恢复合同；不能直接改为字节 CID 比较|
|PERF-054 接收确认|[event_recvack.go:44](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/source/peer/internal/user/handler/event_recvack.go#L44) 普通 Recvack 从内存 RetryManager 移除；SyncOnce/master 另有会话位置更新|[LocalPrivateStore.cpp:436](../../services/message/local/LocalPrivateStore.cpp#L436) 接收确认同步写本地状态、投递索引和事件；保留逐消息持久接收状态，成本不同|旧 SQL 5ms 合批虽少约 81.33% UPDATE，端到端没有稳定收益，未推广；native 已有受限批确认接口，未证当前完整系统收益|比较就绪确认批或持久游标+空洞，验证乱序、重复、跨用户 ACK、重启、崩溃与恢复；不能直接删除持久状态来复制成绩|
|PERF-062/083 后台派生|[event_persist.go:108](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/source/peer/internal/channel/handler/event_persist.go#L108) 提交成功后推进进程内分发事件|[LocalEventRelay.cpp:113](../../services/message/local/LocalEventRelay.cpp#L113) 读取持久事件，再经 RocketMQ producer、broker/proxy、独立 [UnreadProjector.cpp:475](../../services/projection/unread/UnreadProjector.cpp#L475)、owner 快照 RPC、Redis 和 SDK ACK。已经没有原私聊 SQL Outbox 写入，但完整后台链仍在|健康诊断 Message 约 1.728 核，MQ proxy 0.957、broker 0.750、projector 0.408、Redis 0.242；各组件为 CPU 秒/墙钟秒。42000 事件体与留存档案逐字节一致，Redis 20000 计数准确，投影错误与重试/死信记录均 0|按 Peer 减少每消息派生工作，先验证增量、乱序、重放、重建及部分失败。不能停止后台或延后欠账后声称提速|
|PERF-087/093/094 用户对排队|[event/handler.go:87](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/source/peer/internal/channel/event/handler.go#L87) 保持频道 processing；[event_onsend.go:8](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/source/peer/internal/channel/handler/event_onsend.go#L8) 依次权限、persist、sendack|[GatewayServer.cpp:475](../../gateway/GatewayServer.cpp#L475) 将两用户排序生成同一顺序键，FIFO 覆盖权限/RPC/持久化/未读读取。相邻双向发送在长链上互等。释放 OS 线程不会自动消除逻辑前驱等待|健康暖样本先方向排队均 0.274 ms，反方向 5.612 ms；源代码与相关性一致。旧 ready4 和精确 key 完整对照均无稳定收益，保持关闭|先缩短前驱处理链；新异步调度需要验证实际释放工作线程、顺序、epoch、撤权、取消和 Drain。不能把用户对拆成单向键绕开合同|
|PERF-121/125 冷用户登记|Peer 不经过 Tiny 的 SQL 外键引用登记|旧登记全局 mutex 覆盖 SQL；已用同 actor 的 in-flight 协调，SQL 期间释放全局锁|冷登记两方向 P99 观察改善 59.16% / 59.76%，暖收益不稳。当前组合显式启用，原默认关闭；暖样本登记均仅 0.007 ms|新鲜历史反向对照及用户删除/owner 失联合同；不把冷路径收益泛化成稳态收益|
|PERF-126/130 健康共锁|所核对 Peer 发送提交路径没有 Tiny 外部发布器 `Stats` 准入门槛|旧 `Healthy→Stats` 与后台扫描共 mutex；已改成同启动、失败和停机临界区更新的原子健康状态|R29 四轮 8192 消息、ACK P99 两方向观察改善 28.85% / 16.59%；R30 修复 bad_alloc 错误路径并完成真实终止恢复。当前组合健康 ON 后暖均 1.067 µs，该 mutex 子阶段不再执行|无诊断反向完整对照、长稳及 broker 成功后进度落盘前故障窗口；保留失败 fencing|
|PERF-141 同步 RPC 线程复用|Peer 发送链在本进程事件处理/存储组合，未走 Tiny 的进程间 gRPC hop|[MessageRpcPollerPolicy.h](../../services/message/server/MessageRpcPollerPolicy.h) 当前空闲 poller 上限 2；健康诊断 632 条持久 RPC 采样有 627 个不同 handler TID。应用 callback dispatcher OFF，不是共享 callback 队列堆积证据|同一 R30 ELF 新隔离臂仅把 `MAX_POLLERS` 设为 32，`MIN_POLLERS=1`，完整 42000 诊断及反向 OFF 已执行，见第 6 节。源码 SHA 与已资格验证 ELF 的构建源一致|先完整 backend 核对，再比较线程复用、CPU 和同 MID 的 RPC/排队变化；单臂不能推广。旧 SQL 64-poller 负结果保留，须反向及无诊断正式对照|

Peer 的代码是 Go、TinyIMX 是 C++，能借鉴的是工作合并、所有权、状态和提交边界；机械复制语言代码或移除业务保证不会形成等价优化。以上接口/边界都有具体落点，不以“采用某架构”代替实际减少的工作。

## 3. 当前延迟到底花在哪里

健康诊断 `perf139-n10k700-r3`，同 10000 活跃连接 / 700条/s / 60s，42000 全量成功、完整后台健康；阶段日志打开，因此不与正式 P99 混算。[同 MID 分解](../../benchmark/results/peer-20261017/PERF138-healthy-stage-analysis-r139-r3/result.json)，[原始共同样本](../../benchmark/results/peer-20261017/PERF138-healthy-stage-analysis-r139-r3/same-mid-rows.json)。

405 条确定性共同暖样本的平均值：

|阶段|平均耗时|含义|
|---|---:|---|
|Windows 入队到写 socket|0.021 ms|当前证据不支持把主要差距归于客户端发送排队|
|Gateway 用户对队列|3.345 ms|包含等待同键前驱，不能全部称为工作线程不足|
|权限判断|0.820 ms|本地有界端口仍查 SQL|
|路由|0.369 ms|独立主路径阶段|
|持久化 RPC 全程|4.819 ms|包含下面的 handler 与 RPC 外侧，不能重复相加|
|RPC handler 内|3.551 ms|包含 native 3.426 ms|
|RPC handler 外侧|1.268 ms|传输、入口前处理和客户端调度的混合，不能叫纯网络 RTT|
|native 内 CID SQL 桥|0.787 ms|native 内部子阶段|
|native 内用户锁|0.520 ms|native 内部子阶段，双向争用明显|
|native 内同步引擎写|1.941 ms|native 内部子阶段；不是裸磁盘理论延迟|
|ACK 前未读读取|0.705 ms|持久化 RPC 后仍有一次主路径工作|

阶段 P99 不可相加。上级阶段与子阶段不可相加。所有阶段均值也不能拿来分摊另一轮正式 P99。应用 callback 队列等待暖样本均约 0.089 µs，实际 callback 开关为 0；这不排除同步 gRPC 自身线程/调度成本。

## 4. 为什么之前没有优化到位

1. 早期多数改动缩短 SQL/RPC 往返或调整队列，但保留了主要逐消息工作。源码审计说明高成本链仍然存在；线程矩阵、ready 分片和简单合批的多个负结果已足以停止盲目重复。
2. 本地存储、并发登记、健康共锁的改动已经做出并实际运行，但它们仍是隔离候选。当前正式完整点证明候选可处理该负载，不代表已经替换原 SQL 默认或完成容量、长稳、故障和反向复验。
3. 当前候选仍有逐消息 SQL 兼容桥、持久接收确认、RPC hop 和重后台派生。已移除原 SQL 新消息事务，不等于已对齐 Peer 的整个消息流水线。
4. 有些成本来自不同保证，有些是可以减少的额外工作；必须逐项验证。不能把全部 CPU 差距用保证差异解释掉，也不能为了分数直接取消可靠性或权限。
5. 发现过实验工具错误。长内部 DNS label 让 projector 无法调用 owner RPC，即使客户端 42000 条成功、原 topic 消费尾为 0，Redis 仍未更新。该轮被拒绝为健康性能证据，未拿它的低后台 CPU 当作优化成绩。

## 5. 实验失败必须闭环

|问题 ID|已经执行的动作|结果文件与比较|下一验证|
|---|---|---|---|
|PERF-138 失败诊断|保留 `perf138-native-10k700-diagnostic-r1`；核对 native、原 topic、Redis、projector 和 retry topic|[失败分段结论](../../benchmark/results/peer-20261017/PERF138-current-stage-analysis-r1/result.json)：客户端/native 42k 正确，但 projector `process_failure=96929`、Redis 0，不能作健康成本分解|不重放或修补该轮缓存；后续轮必须实际 RPC readiness 和完整后台健康门槛|
|PERF-139 DNS/readiness|使用同镜像同网络的正反 DNS 对照：短 label 能解析，69 字符 label 失败；在实际 projector 网络调用配置的 owner 只读 RPC|[DNS 正反对照](../../benchmark/results/peer-20261017/PERF139-dns-and-readiness-probe-r3/result.json)。r139r2 的 JDK probe 缺 SDK 组权限已修复，独立证实该轮 0 鉴权、0 消息；失败文件保留|每臂在任何客户端启动前校验内部命名和真实 RPC；不能只校验 TCP 端口|
|PERF-140 重试队列与健康投影|单独枚举本轮所有 owned topic；失败轮有 96977 retry queue positions，未观察到 DLQ，不能把原 topic tail=0 当成全链成功|[失败 retry 审计](../../benchmark/results/peer-20261017/PERF139-failed-projection-dlq-audit-r1/result.json)；[健康轮](../../benchmark/results/peer-20261017/PERF140-normal-projection-quality-r3/result.json) 42k reconciled、20k Redis、process/ack failure 0、无 retry/DLQ 记录|所有新臂加入相同健康门槛，后续还需真正 broker 故障/恢复实验|
|PERF-141 线程保留单变量|复用同 R30 ELF，32 idle poller、min1；无消息顺序、SQL 隔离、持久化或后台语义变化|[准备及源码哈希](../../benchmark/results/peer-20261017/PERF141-native-retention-preparation-r1/result.json)；控制为健康 r139r3；结果已完成全量核验，见第 6 节|只有 backend 全通过才比较；改善需要反向和无诊断完整复验；失败保留默认 OFF|

不把上述有限工作点称为硬件“最佳性能”。最大稳定吞吐、QPS、长稳与故障恢复仍未完成；后续需要逐级增负载并以成功率、队列是否增长、全量后台收尾、CPU/内存/磁盘约束决定上限。


## 6. 本轮实际 OFF / 32 / OFF 结果

全部为同二进制、有阶段采样的新鲜历史诊断；每臂 42000 条全量消息、完整 native/broker/Redis/projector/retry 门槛均通过。不是无诊断正式成绩，也没有推广默认。

|轮次|idle retention|ACK 均值 ms|ACK P99 ms|投递 P99 ms|Message CPU 核数|Message system 核数|持久 RPC 采样/不同 TID|
|---|---:|---:|---:|---:|---:|---:|---|
|off_before|0|10.7118|29.7978|29.9734|1.7278|0.6271|632 / 627|
|retention32|32|6.2107|19.6215|19.8899|1.4714|0.4638|533 / 22|
|off_after|0|6.3348|18.5487|18.8027|1.5201|0.5294|541 / 539|

[完整比较与逐阶段](../../benchmark/results/peer-20261017/PERF142-retention-off32off-comparison-r1/result.json)。采样线程种类反映复用，不是准确线程创建总量。只有两个 OFF 方向都支持的效果才作为候选信号；宿主页读取、缓存和顺序仍可能影响结果，正式复验仍需要执行。

**本轮取舍：保持默认 retention=0，不把 32 推广为已获性能收益的优化。** 反向 OFF 的 ACK P99 18.5487ms 优于 32 的 19.6215ms，故首对约34.15%的降幅没有在反向复现。32 的处理线程复用确实改善，但对反向 OFF 的 Message CPU 仅少约3.20%，不能将首对14.84%的降幅泛化。停止重复这组线程参数，下一项转向 Peer 消息/元数据 DB 分片与就绪提交组织的源码消融。

## 7. 固定 Peer 的实际 8/1/1/8 DB 分片消融

已完成同配置、每轮 10000 活跃连接/700条每秒/60秒/128B 的四轮实验，共168000条；全部消息、5000个对话的完整历史和保护项通过。固定 Peer 的主消息/元数据 DB 数量变化，slot log DB 保持8个，其他配置按归一化比对相同。

|Peer 轮次|主 DB 数|ACK均值 ms|ACK P99 ms|投递 P99 ms|整机忙核|
|---|---:|---:|---:|---:|---:|
|r25|8|1.5820|12.6985|13.7933|0.98614|
|r26|1|1.7060|13.1330|14.6481|1.10915|
|r27|1|1.6907|12.8669|14.2862|1.10578|
|r28|8|1.5753|12.6366|13.6606|0.98462|

[逐轮完整比较](../../benchmark/results/peer-20261017/PERF143-peer-layout-comparison-r1/result.json)。8 DB 在两方向约减少11%忙核，P99约改善3.31%/1.79%；是实际但有限的工作点收益，不能解释当前Tiny约5.72倍CPU。该实验还同时改变总memtable容量，缓存/宿主页读取存在差异，不作为随机化统计结论或容量上限。

Tiny 的 CID、消息、索引、事件和未读状态有跨键原子性要求，因此不直接抄成8个独立DB。下一项使用实际Message CPU调用栈辨别计算/分配/RPC等工作量，再设计保留原子性的单项消融。无外键政策已另行完成代码与限定合同验证，见 [PERF-146](FK_FREE_CHANGE_20261020_R146.md)，不把架构规范调整包装成已经获得的CPU收益。


## 空扫描完整链记录与正式性能拒绝（PERF-151/153/156）

四轮共168000条消息及完整后台链通过，相同九个固定程序、零FK新空间、无阶段采样。前三轮正式资源门槛通过，最后OFF因主机最低5.4029GiB低于原6GiB门槛而被拒绝，正式ABBA未完成。全部数据及拒绝见 [比较和失败记录](../../benchmark/results/peer-20261017/PERF153-empty-scan-full-abba-r1/comparison.md)。默认保护开关仍关闭，未宣布Peer追平或容量达标。

最后OFF因控制NIC DHCP失败改走既有VIP执行SSH，压测数据路径未改变。此偏差不支持严格单变量因果结论，后续成组对照统一控制路径。下一项是非空队列重复扫描的有界索引消融：先保持落盘格式/确认边界/恢复路径，验证并发提交、发布失败、隔离重入队和关闭，再测完整系统。结果工具的继承标签和跨试验指针已保留并纠正，实际运行资格以PERF-152逐项证据为准。

## 2026-10-21 继续穿透源码的结果

有界键索引已完成完整 ABBA（R157/R162），整机 CPU 仅减少约 1.97%，ACK P99 上升约 2.69%，默认关闭。前置过滤 held 的第一完整正向对照也只有约 1.26% CPU 改善，P99 上升约 2.15%（R169/R172），未推广。不能用其微观解码收益代替整机收益。

进一步确认的具体差异和措施：

| TinyIMX 代码中的额外工作 | 当前执行结果 | 下一项验证 |
|---|---|---|
| 待发布队列只空一个位置仍读取页长 128 条的数据，解析后未入队的尾部反复被读取 | R173 真实线程实验证实；按空位限页已通过顺序、内存和终止恢复验证，R175 冻结 | 正式完整控制组；当前因 8 GiB 发压前内存门槛拒绝，未发流量 |
| 原生未读投影继承逐条读取回退，批量接口仍发 N 次 RPC；Peer 的本地存储/元数据路径没有相同的跨进程调用结构 | R180 已增加有界线协议批量，真实网络证明一批一次 RPC；错误全批拒绝和旧服务共用截止时间通过 | 相同九程序、批量投影固定，只改变新 RPC 开关，检验整机收益 |
| MQ ACK 的应用批量只是并行 N 次单 entry RPC，未合并线协议请求 | 已固定官方客户端/代理源码，发现逐条回执及部分成功语义必须保留 | 核对真实代理配置后扩展 SDK；不能直接把顶层 MULTIPLE_RESULTS 当作全部成功 |
| SDK 接收回调在判日志级别前构造 DebugString | R179 核对实际库哈希与反汇编：调用 0x22859f，日志级别读取在后；不是只看源码 | 在版本固定的 SDK 源码测试目标中测量/修复；R183 直接探针因私有嵌入 protobuf 无动态导出链接失败，没有计时结果 |

详见 [R180 实现、失败与验收](NATIVE_UNREAD_BATCH_RPC_20261021_R180.md) 和问题台账 PERF-178 至 PERF-185。上述各项分别记录，不把新的批量快照 RPC 描述为已经改好 MQ ACK，也不宣称当前已达到 Peer 整机忙核。

R187—191 继续把 SDK 的额外日志工作落实为精确补丁：两个上游源文件、12 个表达式，使用延迟 fmt 参数保留 DEBUG 与 backtrace，并支持同一个候选库的独立 OFF/ON 开关。固定源码/API blob、两种 fmt 的 C++11 普通与 sanitizer、完整 SDK 构建和九角色加载已通过。安装 SDK 保持原样；真实 MQ 的四个隔离功能组、格式化的实际 CPU 份额及完整负载收益仍未验证。细节见 [SDK 候选及资源/审批边界](SDK_LAZY_DEBUG_20261021_R188.md)。

后续 R205 已从结束且未挂载的 18 份合成消息快照回收约 406 MiB，完整 E 实际恢复及闭环证明保留，没有删除程序/缓存。R192r3 随后完成相同新 SDK OFF/ON/ON/OFF 四组真实 MQ 功能，共 128 条，正文/键、缺失回执拒绝、真实重投、停止后失败及实际 DEBUG 路径全部通过；此前端口假设和依赖配置两次失败均保留。功能客户端是 guest 到既有 Docker backend，不能替代 Windows 完整发压或推出 CPU 收益。R209 原配置正常重启及 510 个文件校验通过，R210 内存仍低于 8 GiB；R212 固定九程序新范围已经 stage，但正式启动被 R213 自动审批拒绝，零正式流量。下一步仍为原门槛通过后的限页完整单项控制，随后独立验证 SDK 与批量快照 RPC。

R217 已用同一候选静态协议/protobuf 和实际 lazy helper 测出文本构造组件成本；真实旧正文 364B 的两个记录约 0.76–0.79 μs，加合成响应字段约 2.66–2.70 μs。实际频次和整机收益仍不明确，不据微循环推广默认值。R218 继续落实已知的 MQ 多 entry 障碍：固定 C++ SDK 的聚合回调丢失逐项响应，不能仅将请求拼大；先完成唯一 ID 乱序映射、续期回执、聚合一致性及权限错误解析器，两种编译配置各 1,534 检查通过。实际批量 SDK/API 和 RPC 数减少仍未实现，剩余接入步骤见 [线协议批量记录](MQ_WIRE_ACK_BATCH_20261021_R218.md)。

R221—229 已继续落实上述 SDK/Main 线协议候选：三个 SDK 对象隔离重编；Main 在默认 OFF 的版本化能力校验后接入，受影响三角色独立链接、18 次原/新 SDK 加载和能力契约通过。SDK 与 Main 两层受控 gRPC 各 18 用例，服务端各收到 18 请求/385 项，逐条错误不丢失。四组真实 Broker 共 128 条，漏确认重投及超过不可见期后无重投通过；两组 ON 每组 32 有效条目由 3 次 SDK 批量提交确认。该计数不能直接套到正式业务批次分布或整机收益。

对原先“代理实际配置未核实”的问题也已追查：R227 读取运行 jar/class 并反汇编，R229 核对实际文件、Java CLI 和当前启动日志，确认启动值 `enableBatchAck=false`。故本项仅合并客户端到 Proxy 请求，未宣称 Proxy 到 Broker 内部调用也合并。服务端开关必须独立测试，不能与新 SDK 同时打开后合算改善。R226 宿主仍仅约 5.34–5.37 GiB，正式控制未启动；原九程序、SDK、容器、数据库及 binlog 保留。当前仍未以完整同负载实测证明追平 Peer。完整问题 ID、源码、失败与后续验证见 [更新的 ACK 记录](MQ_WIRE_ACK_BATCH_20261021_R218.md)。

R235现已在同镜像独立Proxy抓取实际Broker请求：同一SDK客户端均3次批量调用，服务端false时两组各32次单条ACK，true时两组各3次批量ACK。R236继续实测坏回执，发现false仅1项失败，true同批16项失败，均经真实重投恢复32条；不因请求变少就开启默认值。R237还将36次发送拆成32次本次业务和4次系统心跳，保留实际帧字节、命名长度校正限制和旧计数不变校验。剩余完整负载/投影幂等/CPU/P99与持续容量按原门槛验证，详见 [实际分支及失败范围](PROXY_ACK_BRANCH_20261021_R235.md)。

R243已验证真实原生relay/RPC/Redis在中间坏回执、已读推进及旧事件重投下最终一致，保留整批失败额外工作的代价。R246发现正式程序缺实际SDK计数，R247已接入只在停止摘要读取的观测，R248逐用例与SDK原始计数/真实gRPC对端一致。不能仅凭开关推定有效批量，也不能据此宣称CPU差距已消除。详见 [R243—249](NATIVE_ACK_RECOVERY_STATS_20261021_R243.md)。

R255/256实际镜像复验纠正了容器加载证据范围：镜像LD_LIBRARY_PATH优先选择/opt/tinyimx/lib旧SDK，R247的6项MQ loader日志（包含old/wire标签）均解析到该旧库。此前18项检查仅证明依赖可加载，不证明新SDK选中。显式优先路径、三个MQ角色的只读挂载与实际选中SHA已在9项镜像对照通过；原/新库6项正向控制符合。宿主侧显式候选路径的功能实验仍有效。独立SDK正式控制器还需接入绑定并核对运行映射，未启动完整性能轮。详见 [实际镜像路径缺陷及修正](SDK_IMAGE_BINDING_20261021_R255.md)。
