> 2026-10-27 最新续作位置：已查清1250/s控制组真实E积压触顶65536，以及批量候选在LOCAL模式返回外来批次UID导致全部156507回执无法匹配。原179388成功消息与131084隔离/48242待发布已逐键核验；有限真实2/16消息通过同一18条原数据的无网络闭库审核。见 FAILURE_ROOT_CAUSES_20261027.md、PERF-1772至1776。原20服务健康，保留4446容器。当前LOCAL wire-batch在三个实验入口提前拒绝。下一个实际优化应仅改变Native publication.workers=4→8、保持其他口径一致；规划 benchmark/results/publication-workers-candidate-r1777/plan.json 尚未执行，不能报性能收益。九产品仍是CUD，SDK55不变。不要再次盲测不兼容批量；不要重跑WuKongIM。发布同步继续等待最终自身压测。

## 2026-10-27T03:40:15.028062+00:00：112GiB扩容、退役消息清理与新SDK完整资格

- PERF-1695：真实单文件SDK修复、LOCAL/CLUSTER各207业务+23MCP、实际32条崩溃/丢ACK恢复已通过；功能修复没有产生新的性能胜出数据。
- PERF-1722 / PERF-1725：APPROVED112GiB_FULL89687_FILES_SQL20_SNAPSHOTS_4368_AND8C16G_PASS。核验89687条文件记录和68724857264字节内容、20张SQL表、原4368身份与8核16GiB；根盘快照保留，精确VMX恢复。
- PERF-1726：RETIRED942_ACTUAL_ALLOCATED_BYTES_RECLAIM_ALL_RECORDS_4368_AND_SQL9_PASS。按用户最新清理要求，选择退役942的六个MQ消息store，E压缩归档及实际恢复哈希后回收；保留全部迭代、代码和结果记录。
- 下一实际实验：固定WuKongIM300秒完整30万消息；同新SDK、总堆4GiB、G1及SYNC_FLUSH条件下进行CLUSTER/LOCAL的ABBA。历史MQ为ASYNC_FLUSH，不能把新对照与旧数字合并成单因素结论。随后扩大40K用户、时长和速率。
- 完整目标仍未完成；没有宣称最大QPS、持续容量或追平WuKongIM。

更新：2026-10-27T00:14:00.808824+00:00。LOCAL MQ实际32条的发布回执、进程崩溃后完整恢复、丢回执重投、批量确认与正常重开已通过；仅为RAM功能资格。实际旧SDK空路由遗漏完成回调已由190秒卡住的进程、当前输入源码SHA和32条完整存储独立核对定位，失败分支补丁已写，真实对象回归状态：NOT_YET_STARTED。

新的完整207/MCP23状态：RUN_COMPLETE_REAL_WINDOWS_TCP_GROUP_FILE_PERMISSION_MCP_SUITE；父恢复状态：NEW_BOOT_ORIGINAL4_VERIFIED_RUN_LOCAL_MQ_COMPLETE_BUSINESS。前轮62项因测试路径仍指向旧磁盘目录而停止，原测试存储含Redis受限目录已只读导出、E盘实际恢复/SHA、回收仅14个新临时容器、原4368与原20恢复。原程序、原库、binlog、受保护9库、镜像快照均保留。

当前完整性能仍以1615四轮120万消息为准：回复ON ACK均值5.973492ms、P99 23.0262ms、整机5.591690核。没有以MQ功能资格或旧60秒peer数据宣布性能追平；同磁盘长A/B、peer300秒、容量上限和更高用户阶梯仍须继续执行。

本轮每项动作/文件/比较/下一验证：../../benchmark/results/local-MQ-and-callback-followthrough-r1703-r1/result.json

---

# 当前实测与下一项验证（2026-10-26T22:32:03.922456+00:00）

已验证并保留ACK前Redis冗余往返优化；主要剩余开销为MQ代理/Broker及Message/Gateway多段处理。后台1ms收拢未完成预定四轮，不推荐默认开启；转向已实际启动的官方MQ本地模式，先完整语义资格再比较CPU。真实中止轮15待补投已在独立副本完成恢复和全181285索引验证。

第四轮状态：SPECIFIC_ACTUAL_GATHER_ABBA_FAILURE_PRESERVED；完整轮数：3。中止轮的实际181285条去向见PERF-1676；真实15待补投的副本恢复见PERF-1682。
PERF-1668：909个未绑定编译缓存已完成E盘归档、实际恢复/SHA后回收，所有业务存储和容器保留。
PERF-1661：精确实测源码已独立恢复校验归档。PERF-1679-r6：官方MQ本地模式已真实启动，LocalMessageService实例存在；完整业务与性能资格仍待执行。
最大稳定QPS、40K实际业务资格、600秒与更高阶梯负载仍为后续执行项；1000消息/秒是本轮提供负载。
具体动作、结果文件、比较与下一验证：benchmark/results/current-root-cause-actions-r1672-r3/result.json

---
# 当前实测与下一项验证（2026-10-26T22:23:42.713951+00:00）

已验证并保留ACK前Redis冗余往返优化；主要剩余开销为MQ代理/Broker及Message/Gateway多段处理。后台1ms收拢未完成预定四轮，不推荐默认开启；转向已实际启动的官方MQ本地模式，先完整语义资格再比较CPU。真实中止轮15待补投已在独立副本完成恢复和全181285索引验证。

第四轮状态：SPECIFIC_ACTUAL_GATHER_ABBA_FAILURE_PRESERVED；完整轮数：3。中止轮的实际181285条去向见PERF-1676；真实15待补投的副本恢复见PERF-1682。
PERF-1668：909个未绑定编译缓存已完成E盘归档、实际恢复/SHA后回收，所有业务存储和容器保留。
PERF-1661：精确实测源码已独立恢复校验归档。PERF-1679-r6：官方MQ本地模式已真实启动，LocalMessageService实例存在；完整业务与性能资格仍待执行。
最大稳定QPS、40K实际业务资格、600秒与更高阶梯负载仍为后续执行项；1000消息/秒是本轮提供负载。
具体动作、结果文件、比较与下一验证：benchmark/results/current-root-cause-actions-r1672-r2/result.json

---
# 当前实测与下一项验证（2026-10-26T21:34:05.954938+00:00）

已定位并验证ACK前Redis冗余往返；剩余主要成本集中在MQ代理/Broker、Message及Gateway多段处理。收拢后台RPC的CPU收益目前很小，按完整四轮决定，不把局部收益称作追平。

第四轮状态：P4_UNTRACED300S300K_RECEIVE_GATHER_OFF；已独立关闭轮数：3。
PERF-1668：909个未绑定编译缓存已完成E盘归档、实际恢复/SHA后回收，所有业务存储和容器保留。
PERF-1670/1671：四轮关闭后自动保存精确实测源码，离线审计官方MQ本地服务实现，并盘点新peer写入预算。
最大稳定QPS、40K实际业务资格、600秒与更高阶梯负载仍为后续执行项；1000消息/秒是本轮提供负载。
具体动作、结果文件、比较与下一验证：benchmark/results/current-root-cause-actions-r1672-r1/result.json

---
更新：2026-10-26T20:00:47.393505+00:00。后台收拢候选真实循环与Redis0/1000us合同、207完整业务、MCP23全部通过；同9程序中只有投影程序新编译，其余8个哈希保持。完整业务宿主最少10.400GiB，原20及所有4320容器/数据恢复。候选默认关闭。

正式四轮1644-r2已启动：P1_UNTRACED300S300K_RECEIVE_GATHER_OFF。仅收拢0/1000/1000/0，回复固定1，每轮20K全活跃/1000消息每秒/300秒；收益等待真实闭合，记录每组件CPU和后台工作数。此前回复单因素四轮120万完整成功，ACK均值7.147658→5.973492ms，P99 26.4602→23.0262ms，CPU5.619978→5.591690。

peer1630新对照因实验盘20GiB保护线中止，1434完成不计入性能排名；原20/源数据恢复。已实际归档恢复并回收1.29GiB测试缓存和544MiB包缓存，另对108份相同封存ELF做保内容/路径去重释放3.63GiB。所有数据库/binlog/容器/快照保留。新正式流量前检查额外写入预算；peer后续新命名空间将预留4GiB增长。所有问题已有动作、结果文件、比较和下一验证。

证据：[当前资格、清理和问题后续](../../benchmark/results/gather-full-business-and-next-formal-checkpoint-r1656/result.json)、[正式对照](../../benchmark/results/peer-20261017/PERF1644-receive-gather-long-ABBA-followthrough-r2/result.json)。以下为历史检查点。

---

更新：2026-10-26T18:49:40.767951+00:00。四轮正式单因素对照已全部闭合，20K全活跃用户、1000私聊消息/秒、每轮300秒，120万消息全部完成客户端、首次投影、独立持久化/确认/索引、资源与原20恢复检查。

|各组实际600000条；同一9个程序|回复快照OFF|回复快照ON|
|---|---:|---:|
|ACK平均|7.147658ms|5.973492ms|
|合并原始样本ACK P99|26.4602ms|23.0262ms|
|计划到投递P99|26.7659ms|23.2817ms|
|整机平均忙核|5.619978|5.591690|

ACK P99改善12.98%，满足预设延迟保留条件；CPU仅下降0.50%，后台成本仍是主线。候选默认关闭，未更新原生产20服务。真实代码资格包括4种Native组合、8次真实退出恢复、同步/异步RPC、207业务和23项MCP。

四轮后台计数与CPU分解已保存：Gateway/Redis节省被其他活跃工作大部分抵消；发布、确认、投影批次更碎是待干预验证的具体路径。默认关闭的后台收拢代码1626已写，尚未编译/验收。固定WuKongIM新300秒对照1630已启动，不用旧60秒替代同窗口结果。

证据：[四轮完整结果](../../benchmark/results/final-unread-ABBA-and-remaining-CPU-r1633/result.json)、[原始正式闭合](../../benchmark/results/peer-20261017/PERF1615-committed-unread-long-ABBA-followthrough-r1/result.json)。以下均为历史检查点，保留故障和修复来源。

---

更新：2026-10-26T17:39:04.754602+00:00。新未读回复代码已通过实际资格与完整业务：4种Native组合、8次真实进程退出恢复、RPC身份/计数及原顺序/停机合同通过；完整207业务、23项MCP与原20恢复通过，同一9个程序哈希一致。

新四轮正式对照已启动，当前为P2_UNTRACED300S300K_UNREAD_REPLY_ON。首轮关闭控制：300000/300000，零负ACK、重复、超时；ACK均值7.170133ms、P99 25.8425ms、计划到投递P99 26.1788ms、整机5.624630核。首次原子投影、独立全部持久化/确认/索引、资源、恢复通过。新候选开启轮尚未关闭前，不报告优化收益。

当前实际CPU成本核对：旧同负载控制首60秒MQ Proxy+Broker约1.835609核，双Gateway约1.207935核、Message约1.225166核。这是继续压缩跨服务和后台工作量的实际依据。

证据：[已验证的代码与业务](../../benchmark/results/actual-unread-code-business-and-first-control-r1621/result.json)、[正在执行的四轮对照](../../benchmark/results/peer-20261017/PERF1615-committed-unread-long-ABBA-followthrough-r1/result.json)。以下保留此前故障与修复来由，构建阶段状态已由本更新取代。

---

# 当前源码差距、实际故障和修复状态

检查点：2026-10-26T16:38:54.063286+00:00。当前任务继续推进；没有将源代码已写入等同于优化已通过。

问题已经落到具体位置：

1. **ACK关键路径多一次同步读模型。** Native事务内已经计算未读计数，Gateway在持久化成功后仍租借Redis连接并执行未读读取。新默认关闭候选把同次已提交的快照带回原RPC，移除Created消息ACK前重复的两次PING、两次GET；SQL、旧回复、Reused与writer-enabled保持原修复门。
2. **后台重复工作。** 当前demand1三轮中每300000实际发布对应151.8–153.7万次解码，80.2%–80.5%为held重解析。held候选16合同/8进程恢复和207/MCP23已通过；性能对照的OFF控制组发生独立真实故障，故未宣称收益。
3. **突发准入预算过小且多层一起饱和。** 固定peer每DB有4000批次队列并合并就绪批次；实际Tiny为8 worker、全局128在途。故障秒261 Message拒绝、11本地权限pending拒绝、44权限工作池拒绝，另135不同MID的接收ACK任务拒绝。独立存储审计确认299684成功请求全部持久化和接收确认，其余316没有写入。扩大缓冲的突发恢复能力和持续吞吐改进将分别验证。
4. **实际I/O停顿。** 第82秒IOwait约2.201核、两块虚拟盘写等待上升，宿主内存约9GiB且无交换；Rocks合并与Redis后台保存都没有与这次起点重叠。该故障与旧1530大SST合并停顿分别保存。
5. **CPU工作量差。** 已有同MID冷请求中RPC队列35.379ms、SQL引用16.361ms，自身WAL0.848ms；稳定期还包含MQ Proxy+Broker约1.60核、双网关约1.24核、Message约1.35核。现有Gateway持久化RPC已经异步，继续改成异步不能当成新优化。

SST单因素四轮完整120万消息与所有首次投影/持久化/索引/资源/恢复均通过，但候选没有收益，保持OFF：

|各组600000消息、每轮20K全活跃/1000消息每秒/300秒|OFF|ON|
|---|---:|---:|
|ACK均值|7.272709ms|7.295018ms|
|ACK P99|28.4544ms|28.8589ms|
|整机平均忙核|5.626041|5.613768|

该控制第一60秒ACK均值10.118001ms、P99 39.9103ms、CPU 5.780509核。固定peer既有60秒参考为2.028316/9.7149ms、1.940497核；长窗口降低冷请求权重不会被当成代码收益，后续需要同窗口的新对照。

新回复候选实际9服务构建/真实测试状态：1598-r3运行中。r2新增RPC测试缺链接依赖已修正，旧失败保留。正常重启释放内存且实验盘已恢复；原20与所有4246容器/镜像配置、原消息和9库校验保持。下一项依赖明确为1598真实资格→1606完整207业务/MCP23→1607恢复→同一9程序的单因素长A/B。当前无新性能收益或最大QPS声明。

证据：[实际队列与peer源码](../../benchmark/results/actual-queues-and-peer-source-gap-r1599/result.json)、[故障同窗](../../benchmark/results/failed-control-pause-r1593/result.json)、[独立数据命运](../../benchmark/results/peer-20261017/PERF1580-perf1578-p1-wire-users20k-closed-audit-r2/result.json)、[SST四轮](../../benchmark/results/peer-20261017/PERF1571-sst-long-ABBA-followthrough-r4/result.json)、[源码与资格计划](../../benchmark/results/committed-unread-source-preflight-r1597-r3/result.json)、[挂载与跨启动保护](../../benchmark/results/peer-20261017/PERF1604-postboot-mount-data-recovery-unread-build-r1/result.json)、[问题台账](ISSUE_LEDGER_20261017.json)。
