# 原生消息投影恢复与 ACK 实际计数（R242—R249）

2026-10-21。新增的是有限故障恢复与观测能力，尚无新的正式性能成绩。原 8 vCPU / 16 GiB、Windows 原生发压拓扑和资源门槛不变，SDK/服务端批量均未推广默认值。

## PERF-243：中间坏回执、真实投影和已读推进

固定 R222r3 SDK、R224 Main transport 和官方 Proxy 镜像，仅在临时 Proxy 切换服务端 batch=false/true/true/false。每轮使用独立 topic/group、原生 RocksDB store、真实 LocalEventRelay、MQ、未读快照 gRPC 与 UnreadProjector，写入独立认证 Redis。不是假 writer 或手工修改缓存。

每轮提交32条私聊；relay逐项核对正在发布的正文和原生待发布记录一致。第一批16项 ACK 之前确认缓存已投影32条，在索引8处替换损坏回执。ACK失败后通过原生 MarkDialogRead 持久化已读推进，relay再发1条真实 read_advanced 事件。重投旧事件时必须从持久 owner 重新读取快照，不能把旧 unread 重新加回。

| 服务端组 | 原生消息 / 唯一事件 | 第一批失败 | 实际投影应用次数 | SDK提交项 / 请求 | Broker 单条 / 批量 ACK |
|---|---:|---:|---:|---:|---:|
| OFF1 | 32 / 33 | 1 | 34 | 34 / 4 | 33 / 0 |
| ON1 | 32 / 33 | 16 | 49 | 49 / 4 | 0 / 3 |
| ON2 | 32 / 33 | 16 | 49 | 49 / 5 | 0 / 4 |
| OFF2 | 32 / 33 | 1 | 34 | 34 / 4 | 33 / 0 |

四组累计128条消息、132个唯一事件，实际执行166次投影；不是只执行一次。最后每组33个事件均确认，未出现成功确认后的重投，每个重复事件最多投递两次。失败范围扩大确实产生额外处理，不能隐去这项工程代价。

所有组的私聊/总未读均为0；独立 redis-cli 验证两 key 类型为 none、EXISTS=0，符合零计数删除键的真实 Lua 语义。原生 pending/failed 均空。projector先join再Stop consumer；relay drain/sync，RPC停止后请求失败，store关闭重开后32条消息及历史不变、未读0。没有通过强制清缓存或重置消费位置使结果通过。

实际 pcap 限于自有 Proxy→Broker10911，TCP完整重组并校验 group，kernel drop0。ON 的首个坏请求在 Proxy 解码阶段失败，未发往 Broker，因此 Broker批量数比 SDK提交少1。两次ON分别4/5次SDK请求也说明不能预设正式负载总是“32条合成3次”。

边界：身份注册使用合成 fixture registry；未覆盖生产 SQL actor 注册、权限切换或Windows完整IM。临时Proxy heap512MiB/CPU2、独立Redis128MiB是功能仪器，不用于正式性能比较。此项证明这条真实投影链在特定故障下最终一致和已读不回退，不替代权限、全量42k、长时故障和容量验收。

证据：`PERF243-native-projection-r2/result.json`、四个 `redis-value-proof.json`、`parsed-ack-requests.json`、原始 `capture/ack.pcap` 及逐组客户端日志。

## 失败也保留

R243r1首次磁盘预检拒绝，未部署夹具；独立重新确认空间后才开始。随后OFF1的C++完整链已通过，控制器却把“未读0”误断言为Redis必须存字符串0；真实实现会删除零计数键。该次原始MGET输出未落盘，不能补写成已观察。保留原程序、已关闭store与失败记录，在新r2范围中先保存TYPE/EXISTS/MGET证明再断言；没有重放r1消息。

R247构建成功，但控制器一度取回R224旧路径的结果，R248r1因此在读取stats_fixture时提前失败；未创建mock或发送请求。已保留错误取回文件和初始执行回执，按正确路径取回并核对实际R247结果，再在新R248r2范围完成用例。没有重编/重放来掩盖取回错误。

## PERF-246—248：让正式程序记录实际批量，而非只记录开关

源码审查发现正式projector仅输出received/reconciled等应用计数，不能证明SDK实际合并。新增具体consumer的非虚 ReadWireAckStats，不改 EventConsumer/SDK虚表或对象字段，不增加消息热路径计数或逐条日志。它查询SDK已有计数，明确区分started、批量启用、兼容能力可用、快照读取成功。

统计范围固定为 **process_global_new_api_only**：同进程所有新增API的累计api_calls/rpc_submissions/submitted_entries/rpc_terminals。旧SDK缺能力不等于测得0次RPC；同新SDK OFF计数0只表示没有调用新API。统计不是Broker实际请求总数，也不是前台持久化ACK延迟。

projector Shutdown 已join所有工作后、consumer Stop之前读取一次；最终摘要同时输出应用ACK批量开关与SDK线协议批量开关。只有两个都开启，应用才真正调用新路径。Start/Stop与读统计仍需外部顺序，不承诺并发Stop安全。活跃时各原子读不是跨字段原子事务，验收使用静止快照。

R247仅新编consumer对象与projector，其他八角色使用既有冻结字节，producer对象精确保留；18项旧/新SDK加载检查通过。原九程序、R224/R232组合未覆盖。

R248r2用实际Main adapter到真实gRPC受控对端的18类用例验证每次调用前后API统计与SDK原始快照相同。最终18请求、385条目、18终态，与对端日志逐项一致；混合结果、乱序、缺项/重项/外来项、权限拒绝、共享超时和停止后失败通过。另用三个独立进程验证原SDK OFF、新SDK OFF/ON以及启动前/停止后状态。该测试不发布/消费Broker业务消息，也不是正式42k成绩。证据：`PERF247-main-ack-stats-build-r1/result.json`、`PERF248-main-ack-stats-controlled-r2/result.json`、`verified-cases.json`、`client.log`。

后续正式单变量验收必须核对两个开关、实际库/程序哈希、应用完成数量与SDK提交数量、提交/终态一致性，再结合Broker完整事件/Redis/Retry/DLQ证明后台闭环；正常模式不得有process/ack失败。不能用这385项受控失败用例要求reconciled与提交项相等。

## 空间、保留项和正式门槛

PERF-244将18份已结束未挂载Redis值导出及36份闭环证明先流式保存到E，实际独立恢复54文件并核对全部SHA；再次核对所有2292容器挂载后，仅删除18份原始导出（33,466,154字节），36份证明仍在guest。没有删除程序、缓存、数据库、binlog、容器或快照。E归档：`raw-r244/ended-redis-value-exports-and-proofs.tar.gz`，SHA `54655014c51f27fd296f6a4af9845c149fc72d7739ae99d0ee0317ae868df08c`。

R242/R245/R249各独立连续10秒检查，均未达原8GiB启动门槛；最新R249宿主可用5.018—5.038GiB。正式目录仍不存在，未重试R213自动审批拒绝的正式驱动、未关闭用户工作窗口、未降低门槛。最新隔离构建后guest仅比15%+650MiB保留线多约6MiB，不能在此空间上继续大规模编译或正式写入。

下一步顺序不变：实际资源资格通过 → R175/R212冻结限页控制 → 原SDK/新SDK OFF兼容控制 → 同新SDK客户端OFF/ON/ON/OFF → 正式相同Proxy资源下服务端开关控制。日志与未读批量RPC等变量分别固定。完整42k及顺序、权限、持久化/缓存/消息恢复通过后，才按忙核与端到端P99决定推广；最终WuKongIM追平、极限QPS和长时稳定性尚未完成。

## R250—251：实际恢复和待测组合冻结

R250在E实际独立恢复233个运行文件（122,866,521字节）并逐个SHA通过，含5份pcap、R243已关闭测试store、失败与成功程序/对象/源码；189份元数据另存。排除私有配置和无关日志，没有删除guest文件。原2292容器/20服务、原九程序、原/新SDK、保护SQL/binlog和138缓存再次核验。清单：`<LOCAL_EVIDENCE_ROOT>/native-recovery-stats-r250/manifest.json`，SHA `01ae5923d82b9b3539703a8e3f76391ec3bea68fa94c97e5f6c334650011799b`。

R251从已恢复的R232包与R250新projector组合九角色，仅该程序变化，八角色及整个SDK字节不变；43普通文件重新从压缩包实际恢复、两个Linux别名映射核对。未部署，原R175/R212/R232保持。归档：`<LOCAL_EVIDENCE_ROOT>/ack-stats-runtime-r251/nine-role-ack-stats-runtime.tar.gz`，SHA `a62ad578b47e2159f3500e14ec30c2a7d6b0a66e8b92e445a0fee9276ca21cff`；runtime-manifest SHA `fb07a970b7bd9066f9a852ba89a16c787a5a3ab8f968ef7984fc321d7252e3ff`。统计程序已具备后续正式观察能力，仍无新的正式CPU/P99/QPS成绩。

R255/256实际镜像复验纠正了容器加载证据范围：镜像LD_LIBRARY_PATH优先选择/opt/tinyimx/lib旧SDK，R247的6项MQ loader日志（包含old/wire标签）均解析到该旧库。此前18项检查仅证明依赖可加载，不证明新SDK选中。显式优先路径、三个MQ角色的只读挂载与实际选中SHA已在9项镜像对照通过；原/新库6项正向控制符合。宿主侧显式候选路径的功能实验仍有效。独立SDK正式控制器还需接入绑定并核对运行映射，未启动完整性能轮。详见 [实际镜像路径缺陷及修正](SDK_IMAGE_BINDING_20261021_R255.md)。
