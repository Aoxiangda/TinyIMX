# 当前 ACK 延迟、CPU 差异与实际修复记录

本轮主成绩取完整冷60秒、20000名全部活跃用户、1000条私聊/秒、128B正文；每个方案两轮原始请求合并为120000条。固定WuKongIM为新的同配置60000条完整点。

|方案|ACK平均ms|ACK P99 ms|发送计划到投递P99 ms|整机忙核|
|---|---:|---:|---:|---:|
|A|20.1306|195.9656|195.8470|5.9211|
|B|12.8777|84.0757|84.4313|5.9060|
|C|10.8028|44.5504|44.8281|5.8962|
|固定WuKongIM|2.0283|9.7149|11.1426|1.9405|

A=原64分片队列、逐条sync；B=完整会话键隔离、逐条sync；C=完整键隔离+原子物理组提交。A/B/C均已通过每轮60000条发送/ACK/投递、资源和关闭恢复检查。C为后续实验候选，未覆盖原SQL历史部署。

## Ordering scope

Full conversation-pair FIFO must remain. Disabled experiment also owns the entire modulo64 stripe during suspended RPC; enabled experiment isolates unrelated full keys.

TinyIMX: `gateway/GatewayServer.cpp:479; gateway/business/BusinessExecutor.cpp`。固定上游: `internal/channel/event/handler.go:65,89`。

已执行：PERF1228 A/B, all120000 messages per variant。后续决定：Keep exact-key as part of combined candidate, not a claim that it alone resolves latency.

## Logical batch versus physical durable write

OFF loops scalar sync writes and completes the group together. ON commits admitted creates atomically. Peer also waits for a synchronous commit of ready batches; it does not issue an in-memory ACK.

TinyIMX: `services/message/local/LocalPrivateStore.cpp:434,581`。固定上游: `pkg/wkdb/message.go:44; pkg/wkdb/wukongdb.go:433,481`。

已执行：PERF1138 native contracts/pre-post sync actual interruption; PERF1198 full207/MCP23; PERF1228 B/C balanced actual AB。后续决定：Measured C pooledACKP99=44.5504ms, not peer-equivalent9.7149ms; no original-history deployment.

## Unread read critical path

Current writer-owned projection causes Gateway to lease twice and read private/total separately. Candidate uses one lease and one atomic Lua, keeping health checks, int64 values, missing and per-key wrong-type semantics.

TinyIMX: `gateway/GatewayServer.cpp:EnsureUnreadProjection; services/cache/UnreadCountCache.cpp:GetPrivateAndTotalUnread`。固定上游: `internal channel/conversation in-process processing, no identical pair of Gateway Redis GET calls`。

已执行：PERF1231 source and real Redis1000-concurrent-snapshot contract written; PERF1235 actual all9 build completed; PERF1254 real Redis1000 and recovery checks passed。后续决定：DefaultOFF; no measured latency gain claimed before actual business qualification and A/B.

## Duplicate event encoding

Tiny serializes the same domain-event JSON twice for E/ and A/ values. This is a concrete potential redundant CPU operation, separate from required durable outbox semantics.

TinyIMX: `services/message/local/LocalPrivateStore.cpp:362-366`。固定上游: `pkg/wkdb message encoding and shared batch commit`。

已执行：PERF1248 current-ELF profile executed; PERF1250 default-OFF encoding reuse implemented; PERF1254 both-mode contracts and actual crash recoveries passed, no measured gain yet。后续决定：PERF1260 same-new9 separate-factor test required; preserve both durable event indexes and exact bytes.

## Current CPU attribution

C still uses about5.896cores vs peer1.940cores. Current Message/Gateway/MQ/caches all remain. Old R30 iterator profiles predate pending-event indexes and cannot establish the present hot path.

TinyIMX: `PERF1232 per_variant_component_means.C`。固定上游: `PERF1213 whole VM same offered workload`。

已执行：Current untraced component counters saved; PERF1248 fresh exact-owned-ELF diagnostic completed:1020 samples/lost0,60000 complete messages。后续决定：No global cache trimming, shared work-window termination or old-profile attribution.

## Callback execution

Current RPC callback schedules the business frame back to its runner. It does not execute the Redis/business continuation directly on the gRPC callback thread.

TinyIMX: `services/rpc/RpcAwaitable.h:await_suspend; gateway/business/BusinessCoroutine.h:Runner::Request`。固定上游: `channel handler event worker ownership`。

已执行：Source lifetime/scheduler audit; existing frozen lifetime contracts retained。后续决定：Do not describe callback-thread blocking as an established root cause; profile actual runnable work.

## 核验与资源问题已关联的处理

PERF1230首次Proxy启动早于Broker向NameServer注册；已改为真实注册路由就绪，保留原失败。PERF1238精确核对5000条已发布Outbox保留策略清理，停写前后489126行完全一致，跨正常重启后恢复原20服务，原保护检查代码未改。

PERF1240改为正常冷启动后直接从4基础服务交接，不预热原20服务再停止。第一份准入最低可用内存9.45GiB，通过连续10秒8GiB门槛；第一份60000条实际重开历史已从双方20000个用户视角完整读回120000条且一致。其余核验、当前ELF诊断和默认关闭的新候选验证按协调器顺序继续。

单个1000条/秒的60秒成功点不等于最大QPS或高可用结论。未删除权限、持久化、Outbox或接收确认来改变指标语义。

## 当前实现与原始失败保留（PERF1258）

当前正式性能仍是C的ACK P99 44.5504 ms、整机5.8962核，对照9.7149 ms、1.9405核。新代码尚未获得正式成绩，未宣布优化完成。

当前Message确切ELF剖析1020样本且无丢失：WriteThread等待状态自耗5.49%，JSON转义3.63%；这些仅为Message用户态样本占比。两轮C的CPU some PSI约0.51，对照约0.016；设备写入约39MiB/s，对照25.81MiB/s，不能据此单独判定磁盘/CPU的请求延迟贡献。

新增未读原子读取与事件编码复用均默认关闭。新9程序已真实编译；两模式持久化合同、四次真实故障恢复、9加载、1000次Redis并发快照在guest通过。宿主机总过程5GiB检查仍失败，最低4.893GiB；原失败不改写。PERF1256/1257正在以正式8/6门槛完成完整业务资格，然后由PERF1260分别检验两项收益。

六份历史中的C1已经从20000个视角读回120000条一致消息视图；其余5份仍待完成，不把待核验写成通过。当前分析不使用旧R30剖析、不以逐条持久化强度差异替代实测、不关闭权限/MQ/Outbox/接收确认来改善数字。


<!-- PERF1293_CURRENT_NATIVE_PHASES -->

当前原生路径阶段闭环（PERF-1293 / 1296 / 1297，诊断轮）

- 固定新9程序、固定SDK；exact-key=1、物理持久化批次=1；两个微优化=0。
- Windows原生TCP客户端，原Ubuntu8vCPU/16GiB，同机服务；20K全部活跃、1000私聊/s、128B、60s。60000/60000，负ACK/重复/超时均0。原20服务、受保护数据和3607个既有容器全部保留恢复。
- 全60s诊断ACK平均12.1341ms/P99 48.1099ms，整VM5.9167忙核；此轮开启追踪，不替代既有无诊断C组P99 44.5504ms，不宣称改善。
- 834条独立MID抽样：客户端入队到write平均0.0191ms/P99 0.0467；网关执行器排队平均3.3262/P99 27.006ms；网关持久化等待平均5.6563/P99 32.151ms；权限平均0.7913/P99 3.982ms；Redis未读平均0.6181/P99 2.755ms。
- 其中447条具有完整Gateway+持久化RPC+原生存储的关联链：RPC准入队列平均0.5762/P99 6.916ms；原生总耗时3.4142/P99 11.485ms；引擎写入同步平均1.9939/P99 7.239ms；本地锁0.5221/P99 4.654ms。67条实际触发初次引用登记SQL的消息，该段平均2.9302/P99 9.22ms。
- 完整关联样本不覆盖所有物理批次，不能将447条分布当作60000条总体。上述阶段有嵌套，不能叠加P99；也不将跨时钟残差称为纯网络耗时。
- 前20s（131完整链）ACK平均17.8154ms，20s之后（316完整链）平均8.1954ms。冷身份/引用成本是问题之一，之后仍有队列、同步和RPC开销。
- 实际关停：60000次接收确认提交、57542个消息提交批次、58579次引擎WAL同步、14467个发布进度批次。60000投递、确认和投影均闭环，所有pending/held/quarantine为0。

分析器原先用旧p2轮次推断exact-key，产生false元数据；已按实际runtime=true修正，旧报告和源码存于PERF1296-exact-key-metadata-repair-r2，原始时间完全未改。

独立只读持久化核验（PERF-1299）

- 逐条对照60000条真实客户端MID/CID/双方身份/128字节载荷，确认记录state=ReceiverConfirmed且confirmed_at完整。
- P/接收待确认及E/待发布索引为空；只读打开前后所有存储文件SHA完全相等。
- 两个反例（改CID、改发送者）均按预期拒绝。首次工具编译使用了旧RocksDB裸指针接口，编译失败记录保留；修复为当前11.8接口后实际通过。
- 证据：benchmark/results/peer-20261017/PERF1299-perf1293-p1-wire-users20k-closed-store-audit-r2/result.json。

下一项实际验证

PERF-1281/1282：同9程序，关闭两个未证实微优化，组合启用持久化接收确认合并、投影批处理+真实批量快照RPC、RocketMQ线上的ACK批次。先207项完整业务与23项MCP、权限、文件、停机回归，然后PERF-1302/1304同负载C/BG/BG/C。每个正式点再次逐条核验60000个持久化记录。

三项均保留原先成功答复前的持久化/权限边界；没有取消接收确认持久化，没有跳过队列投影。预先登记保留规则：全部业务、资源和逐条关闭核验通过；两轮汇总CPU至少降低5%，汇总ACK P99不退步。只记录观察结果，两次独立重复不宣称统计置信区间。


<!-- PERF1305_DIALOG_FIFO -->

当前主要排队机制的进一步核实（PERF-1305）

先前的executor_queue_wait不能直接称为工作线程耗尽。GatewayServer.cpp:479先对两个用户ID排序生成同一会话key；chat在5457使用它。BusinessExecutor的隔离key消除了无关会话的stripe碰撞，但同会话的异步任务仍保留逻辑lane直到done，释放OS工作线程并不会释放该会话顺序槽。

实际本轮客户端30000对双向发送，均相隔1ms、对端相反；834条发送抽样中：

| 抽样方向 | 样本 | 网关队列均值 | 队列P99 | 客户端ACK均值 |
|---|---:|---:|---:|---:|
| 先发方向 |450|0.5782ms|17.28ms|9.5032ms|
| 后发方向 |384|6.5465ms|35.217ms|14.6559ms|

后发方向中368条的实际write和MID顺序均与前一条一致。其排队时间与“前一条ACK时间-当前write时间”的逐条重叠相关系数0.992948；均值差-0.7373ms，符合客户端ACK收到晚于服务端释放顺序槽的边界。该关联与源码共同定位同会话队首等待；仍不可把每一微秒都归给某个线程或删除顺序约束来冒充性能提升。

缓存的固定上游对比：

- internal/channel/event/handler.go同样有channel processing排他标记；它按批取事件并按类型处理。因此不能说WuKongIM没有会话串行。
- internal/channel/handler/event_onsend.go调用permission→persist→sendack。event_persist.go对一组事件AppendMessages，投递克隆事件单独排入EventChannelDistribute；event_sendack.go把回复投递User事件循环。
- TinyIMX当前lane中包括权限/路由、持久化RPC、未读快照和回复/投递交接；同步写入、跨进程等待被同会话后续消息继承。
- 合法的下一种结构候选应拆开有序append与后续投递/回复阶段，同时保留同会话提交/展示/权限顺序、CID幂等、旧连接fence、accepted预算和关停drain；不能直接把key改为单向或提前done来绕过现有语义。

证据保存于PERF1305-current-dialog-FIFO-overlap-r1，包含384条逐消息对应记录、源文件SHA和全部60000条配对校验。后台组合ABBA进行中；新的结构候选必须在此后独立资格验证与单变量对照，当前没有改动排序语义。


<!-- PERF1304_COMPLETE_BALANCED_ABBA -->

四轮完整无追踪对照已结束（PERF-1302/1304/1311）

原8vCPU/16GiB，Windows原生发压；20K认证连接全部参与，1000私聊/s、128B、60s。相同新9程序/SDK，双微优化关闭；C/BG/BG/C。每轮60000条，合计240000条全部成功，逐条身份/载荷/持久化接收确认、后台排空、资源与原20服务恢复均通过。

|轮次|配置|ACK均值ms|ACK P99 ms|投递P99 ms|VM忙核|
|---|---|---:|---:|---:|---:|
|perf1302-p1-wire-users20k|C|12.179290|52.2331|52.2088|5.937143|
|perf1302-p2-wire-users20k|BG|9.751431|38.2048|38.3807|5.786391|
|perf1302-p3-wire-users20k|BG|10.316755|45.9697|46.1626|5.794769|
|perf1302-p4-wire-users20k|C|12.185540|53.5930|53.8374|5.911601|

两次重复的原始样本合并计算P99，不平均两个P99；CPU按实际覆盖秒数加权。

|配置|消息数|ACK均值ms|ACK P99 ms|投递P99 ms|VM忙核|
|---|---:|---:|---:|---:|---:|
|本轮C控制|120000|12.182415|52.9567|53.1479|5.924373|
|后台组合BG|120000|10.034093|42.1354|42.4962|5.790580|
|固定WuKongIM参考R1212|60000|2.028316|9.7149|11.1426|1.940497|

实际观察：ACK均值下降17.63%，P99下降20.43%，CPU下降2.26%。预登记CPU至少降5%的规则未通过，组合不作为完整CPU解决方案采纳，主线默认开关未改。固定参考并非本次两次独立重复，以上倍率不宣称置信区间或已对齐。

相对固定参考，当前BG均值/P99/CPU仍为4.947/4.337/2.984倍。历史最优C P99 44.5504ms保留；本轮20.43%的改善仅对本轮匹配C52.9567ms计算。

实际批次与物理写入（PERF-1309）：C两轮确认提交均60000；BG分别25310/24983。C引擎WAL同步59223/59862；BG61078/60395。C消息服务物理写10.743/10.999MiB/s，BG10.902/10.885MiB/s。少逻辑提交不等于更少物理同步，不能用逻辑次数代替真实收益。

继续处理：MQ Proxy/Broker在第一对照中合计CPU约1.81核几乎未变。已实际验证缓存Temurin8u482包含jcmd/jfr，离线无网络运行通过；未向原服务附加探针。两个受保护测试MQ物理存储按原样保留，分别31/28个topic、23/20个订阅组、14/13条offset表元项，并非成千上万个遗留topic。元数据只读统计，未改文件权限或删除内容。后续将单独做有限诊断轮JVM方法采样，不把采样轮作为正式成绩。

局部批次的功能完整性已经验证；历史SQL私聊迁移、极限容量、长稳与容量下的高可用恢复不因此宣告完成。


<!-- PERF1324_ACTUAL_JFR_AND_COLD_TAIL -->

当前尾延迟进一步定位（PERF1316/1318–1325）

新诊断60000条全部成功，ACK均值11.2627/P9944.4354ms，持久化逐条核验身份/载荷/接收确认通过；这是带探针诊断，不是正式新成绩。原20恢复、3691容器保留。同步CSV后处理退出1：只有72字节表头、meta.calls0，早于真实发压；析构无条件O_EXCL输出会被继承探针的健康进程抢占。旧失败保留，新源跳过零调用并隔离fork发布，实际复现/修复合同由PERF1322继续执行。

两个测试JVM25秒原始录制已取得、离线解码、E盘31文件40857201字节独立恢复SHA通过，无新发压、无源数据删除。Proxy134个Java执行样本/1085原生样本；Broker154/1121。原生主体epollWait，不能解释成忙CPU。两侧各12次GC，最大6.697/27.330ms；guest wall-mono波动2.424us，311个网关抽样仅5跨GC，306不跨GC仍有尾部，因此没有凭GC出现就改大堆。

448条完整同MID：ACK平均10.1873ms，网关同会话队列3.1082ms、持久化RPC4.2180ms，其中原生总2.5926ms、DB.Write包络1.6856ms。层级嵌套不能相加；DB.Write还包括writer等待，实际sync探针修复前不标成纯硬件同步耗时。

正式原始记录分窗（完整60秒仍主成绩）：

|窗口|BG消息数|BG ACK平均/P99 ms|固定对照平均/P99 ms|
|---|---:|---:|---:|
|前20秒|40000|16.8090 /54.8083|2.8843 /8.0440|
|后40秒|80000|6.6466 /18.1649|1.6003 /10.0227|
|全60秒|120000|10.0341 /42.1354|2.0283 /9.7149|

BG超过40ms的1474条中1430条位于前20秒（97.015%）。首用身份/权威引用属于主要冷路径检查方向；稳定期仍显著落后，不用预热窗口掩盖它。

PERF1322新增默认关闭的存储写线程阻塞等待策略，只改adaptive-yield和max-yield，WAL同步与业务协议不变。r1内存门槛未达，未编译且原20恢复；r2冷启动12.28GiB准入通过，但覆盖层把新增测试源当已有文件，编译前失败，已修正存在/缺失两种恢复。当前真实候选状态：NEW_MESSAGE_ELF_BOTH_WRITE_WAIT_MODES_NATIVE_CONTRACTS_EIGHT_REAL_DEATH_RECOVERIES_PASS（r3）。没有新正式性能收益；构建、两模式合同与真实死亡恢复后，还要完整业务资格及同负载无追踪实测。

PERF1322-r3实际构建14.254秒、两模式合同、8次真实死亡恢复与观测器健康/fork等价合同全部通过，最低6.2959GiB。PERF1328完整207业务及MCP23已通过，最低10.285GiB；PERF1329恢复原20且3703容器保留。正式对照7程序客体编译和28个Java范围合同通过，PERF1334将执行同新9的0/1/1/0，关闭探针，逐轮60000条真实持久化核验。以上业务资格不作为性能收益。


<!-- PERF1334_CURRENT_LIVE_ABBA -->

## 最新实际闭环：SQL简化没有解决尾延迟

以下状态替代旧版文档中的“下一项SQL/完整业务待执行”。PERF1367已经完整结束。
同一8vCPU/16GiB、Windows原生客户端、20K全活跃用户、私聊1000条/s、128B、60s；
四轮0/1/1/0共240000条全部成功，每轮60000条的实际持久化、身份、内容、确认状态、
资源和原20恢复均通过。只改变条件SQL开关；每侧原始120000样本合并，CPU按实际秒数加权。

|指标|同一新ELF控制|条件SQL开启|固定WuKongIM参考|
|---|---:|---:|---:|
|ACK平均 ms|9.639605|10.046951|2.028316|
|ACK P99 ms|38.7592|40.5730|9.7149|
|计划到投递 P99 ms|38.9692|40.8656|11.1426|
|整VM平均忙核|5.766109|5.845118|1.940497|

条件SQL使P99增加4.6797%、CPU增加1.3702%，未过预登记规则，不采用，默认仍关闭。
当前控制P99/平均/CPU分别约为固定peer的3.99/4.75/2.97倍。peer只有一个固定参考轮次，
不宣称置信区间；不同日期控制组之间的波动不写成代码收益。当前结果不是最大QPS或长稳资格。

证据：PERF1367-guarded-reference-ABBA-followthrough-r1/result.json。
源码实际资格：PERF1362同一实际ELF的真实SQL两侧合同与四次死亡恢复通过；
PERF1355完整207/207业务、MCP23/23及8个只读域工具实际后端通过，最低可用9.883434GiB。
MCP仅固定测试主体500001，不宣称已经实现多用户会话桥接。

## 已排除的等待策略及实际提交工作量

PERF1342阻塞等待开关四轮240000条全部成功，P99下降8.7007%，整VM CPU仅下降0.3725%；
未过至少5%CPU收益的原规则，不采用。PERF1364从真实日志读取每侧120000条：
控制WAL同步116331次，阻塞等待124166次（增加6.7351%）。Rocks ticker不是fsync时长；
生命周期日志与正式CPU窗口口径分开保留。

## 仍在解决的提交路径差异及下一项实际验证

PERF1349固定上游集中收取已就绪batch后pebble.Sync；Tiny创建、接收确认和进度更新分开
进入Rocks Write。Rocks本身已有group commit，不能说Tiny没有合批。
PERF1352/1353同MID链与真实反向前驱关联显示：四条采样尾请求的队列16.692—38.048ms，
对应前驱ACK22.8522—43.5410ms，自身身份引用仅0.003—0.005ms。样本4条，
不能冒充总体或把嵌套阶段相加。会话顺序保留，目标是缩短前驱提交而非提前放行后继。

PERF1368代码已实现默认关闭的集中已就绪物理写：原调用者借用已构造batch，
无定时延迟、无新后台线程、64项/128排队/16MiB边界；同步需求取OR，WAL不关闭；
原用户/CID锁保留，不确定结果不重放。PERF1371一并修复存储poison后运行时仍可能报告健康的问题；
性能两侧使用同一修正。此代码尚未写入此前1367测量的实际ELF，不宣称任何性能收益。

PERF1369真实Windows C++调用者组件41条PASS，仅覆盖通用调用者而非Rocks适配器。
PERF1374-r2已在客体编译检查精确生成的两个Python程序，主体执行0。
PERF1373当前状态：COLD_INFRA4_MESSAGE_BUILD_CONTRACTS_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS。
PERF1370当前状态：NEW_MESSAGE_READY_GROUP_MIXED_ENGINE_UNCERTAIN_HEALTH_AND_TEN_REAL_DEATH_RECOVERIES_PASS。
下一顺序：真实Linux构建与混合写/同步升级/不确定返回/10次实际死亡恢复 →
新ELF完整207/23 → 新命名空间单因素四轮及逐消息核验 → 修复后的同步探针独立诊断 →
扩用户、最大持续吞吐和长稳/恢复。任何失败保留原始目录后具体修复，不覆盖旧失败。

## 证据保留与旧启动故障

PERF1354已E盘实际归档恢复：客体168文件472096794字节、宿主613文件291364500字节，
逐文件SHA一致，源文件未删除。该冻结包不含后续1365/1367及1368新代码，
后续单独新增恢复归档，不改写旧包。

PERF1334旧Group50054启动失败发生在发送0条时，不算容量失败。PERF1336实际内核复现临时
源端口占50054导致errno98；PERF1337新增Compose覆盖层仅五服务保留50051—50056。
两组1340/1365全部实际五服务核验相同范围且完成消息闭环。历史占用者未记录，
不把机制实验倒写为历史唯一原因。PERF1345-r1新增Make目标、r2 detached日志驱动和状态元组
工具错误保留；明确离线配置、实际日志和inspect字段后，1362已用同一ELF完成所有SQL恢复。
