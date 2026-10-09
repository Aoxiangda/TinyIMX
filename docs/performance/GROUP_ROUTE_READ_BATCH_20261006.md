# Group online-route read batching candidate (2026-10-06)

Real completion ABBA removed the dominant64 synchronous completion calls (312-329ms to34-45ms), but64-item dispatch remains64-67ms and large groups still fail. Each scalar GatewayRouteResolver::Resolve calls OnlineStatusCache::GetOnlineStatus: one original healthy Acquire/PING, oneGET, originalJSON Deserialize. This is a measured enclosing dispatch phase plus a source-confirmed repeated roundtrip, not a separately measured attribution of all64ms toRedis.

First bounded primitive adds GetOnlineStatusBatch with <=256 inputs for the configured standalone Redis. Empty/invalid-only/oversized requests perform no connection I/O. One original healthyAcquire/PING and one read-onlyLua pcallGET invocation return raw JSON bytes per key. Missing, wrongtype, invalidJSON, unsigned64 identities, duplicates and input order retain original status/record/error handling and C++Deserialize. No TTL/write/caching across iterations; original scalar implementation, existing maintenanceLua scripts and class layout unchanged.

Native test uses only absent own codex:group-route-read-batch-20261006:p1/p4:* keys, TTL900 and two own atomic writer fixtures; it never deletes/flushes/changes production keys or global server settings. Compare all original statuses/records/errors, malformed and binary raw values, ids2^53+1/aboveINT64/UINT64_MAX, max256/reject257, concurrent caller/atomic snapshot/pool lifecycle, raw bytes and TTL. A hiredis argv wrapper counts actual network commands without injecting faults, and is linked only in native test. 40x64 closed-loop OFF/ON/ON/OFF component samples perpool are not capacity/P99. All raw failures and fixtures remain.

This source is not Gateway integration, no runtime selected and no unmeasured performance gain claimed. After primitive conformance, integration must use per-iteration routes only, bound claimed inputs, preserve fresh local connections and peer authentication/durable recipient checks, and run same-image real cross-Gateway ABBA/full functional chains/restoration before mixed capacity acceptance. Remaining2万混合延迟/全部功能5万目标 continuesOPEN.


## Primitive real Redis validation passed (2cbbe23)

Pools1/4: 360 checks each,720 PASS. Scalar/Batch statuses, full records and original errors exactly match, including wrongtype and a valid following record, invalid JSON/binary input, duplicates, UID2^53+1/aboveINT64/UINT64_MAX; 256 exact entries,257 pre-I/O rejection,100 concurrent mixed batches, atomic paired own updates, unchanged raw bytes/TTL and own pool shutdown. Actual redisCommandArgv counter proves64 PING+64 GET to1 PING+1 EVAL, with64 serverGETs retained. No fake network skipping. Native64-user means: pool1 OFF58.035/50.517ms vs ON1.976/2.429ms; pool4 OFF58.252/52.311 vs ON2.848/2.552ms. Forty component samples are not overallP99 or endpoint gain. All19 runtime identities/configs/cache libraries unchanged; owned keys expire naturally and raw evidence remains .local/codex/group-route-read-batch-native-20261006.

## Default-OFF Gateway integration prepared

TINYIMX_GROUP_FANOUT_ROUTE_BATCH_ENABLE=1 is strict and defaultOFF. Only original deferred mode, bounded <=claimed limit<=256 and existing fifth optional callback enable one per-iteration route batch. Invalid durable MID/recipient pairs stay skipped as before; missing callback/oversized/deferOFF keep original scalar sequencing. After batch callback side effects, a malformed result cardinality becomes original retryable failure with no scalar replay. Completion batch remains independent with original lease/status fences.

Gateway uses raw per-iteration cache records and original common route mapping. It continues current Discovery lookup for remote routes, fresh local TCP checks, peer submission/authorization and persistent recipient validation, original ACK tracker, retries and deadlines. No route snapshot survives the iteration; no data fields/class layout change in Gateway or RouteResolver. Coordinator adds an optional function field, so allocating GatewayMain and all coordinator test TUs must be recompiled. Numeric phase marks dispatch_batch; batch dispatch_sum/max represent one callback wall time, avoiding a sum of nested timings. Native integration and actual ABBA still pending; no capacity acceptance.


Integration preparation failures are preserved before build: a local private-section needle matched an inactive comment as well as the active declaration. Partial edits of three local mirror files were snapshotted and only their audited preimages restored, then the active declaration was selected explicitly. No guest file had changed. The first source applier referenced an unversioned package after a fresh v2 package was prepared; it failed before guest audit/stage/source edits. A new helper corrected only that literal and source43fadf0 was committed. Original helpers/packages/audits remain. These were preparation mistakes, not benchmark failures or runtime regressions.

Build reuses the720PASS cacheAPI object only after exact defines/CXXFLAGS/macros/header/source validation; it belongs in libtinyimx_cache_service.a, while the connection-only libtinyimx_cache.a is retained. Accepted conversation/unread and online-maintenance members remain byte-identical. GatewayServer/RouteResolver/Coordinator/allocatingMain and every coordinator test are recompiled. Original128 plus new36 checks and real own namespace GatewayRegistry/Discovery local/remote/offline/malformed/wrongtype/replacement routing will run. Real resolver fixtures haveTTL900 on all records and their own index, with no production registry changes. Only Gateway image is sealed; Message62RPC/protocol checks and exact candidate image stay unchanged. Actual ABBA remains pending.


## 原生接线和真实路由ABBA完成（当前结论）

164协调器回归（原128+新36）、真实Resolver128、Redis primitive720、继承相同Message镜像62 RPC/旧协议1/旧应用层均PASS。构建stage group-route-integration-build-20261006；Gateway image99ee1b4cc953c2c1b0164e770a33888855c429ce2b7f3a11bc4249a493384898，ELF a50e500d05844f56ef731d3491836163ae9d398f3ac32c05b8b0d5fdc06aa6fa。Message f9094e0f/baa2ec54保持。准备时两次助手错误仍保留；本次构建一次通过。源码43fadf0/2cbbe23，构建助手1e4949f，控制6281f3a。

同一Gateway+Message ELF，route OFF/ON/ON/OFF；completion/claimSQL/recipient/defer/commitwake固定1，partial0，原batch64/lease5000/empty recovery1000/ACK retry3000不变。528新消息、23628真实recipient wire及SQL状态3确认、0观察重复；四轮完整216功能交互操作/148断言正确，包含好友、私聊、资料/分页、群治理/权限交叉和每轮1.8MiB文件实际传输/续传/字节一致。每规模每case30测量+3warm，不估总体P99/万人容量。

| 轮次 | 群人数 | ACK均值ms | ALL均值ms | ALL最大ms | 100ms样本门槛 |
|---|---:|---:|---:|---:|---|
| A1 off | 2 | 24.285 | 39.170 | 49.706 | PASS |
| A1 off | 16 | 27.668 | 75.055 | 93.075 | PASS |
| A1 off | 65 | 39.045 | 201.989 | 248.919 | FAIL |
| A1 off | 100 | 59.205 | 324.358 | 389.270 | FAIL |
| B1 on | 2 | 22.585 | 36.980 | 43.368 | PASS |
| B1 on | 16 | 27.950 | 60.307 | 73.371 | PASS |
| B1 on | 65 | 35.839 | 127.534 | 180.114 | FAIL |
| B1 on | 100 | 54.510 | 320.288 | 429.804 | FAIL |
| B2 on | 2 | 23.554 | 38.059 | 45.040 | PASS |
| B2 on | 16 | 28.041 | 60.062 | 75.090 | PASS |
| B2 on | 65 | 38.597 | 124.839 | 160.620 | FAIL |
| B2 on | 100 | 49.181 | 301.513 | 356.665 | FAIL |
| A2 off | 2 | 22.386 | 37.125 | 46.693 | PASS |
| A2 off | 16 | 25.335 | 75.133 | 142.915 | FAIL |
| A2 off | 65 | 39.988 | 201.387 | 268.888 | FAIL |
| A2 off | 100 | 51.821 | 375.175 | 1203.057 | FAIL |

65人ON均值124.839–127.534ms，OFF201.387–201.989ms，至少减少36.672%；16人ON60.062–60.307，OFF75.055–75.133ms。100人ON301.513–320.288ms，OFF324.358–375.175ms，保守均值改善仅1.255%；仍FAIL，不能称100人稳定大幅改善。A2 16人142.915ms和100人1203.057ms长尾完整保留。

| case | 64批抽样数 | claim ms | dispatch ms | completion ms | whole ms | thread CPU ms |
|---|---:|---:|---:|---:|---:|---:|
| A1 | 66 | 29.954 | 61.782 | 35.731 | 128.137 | 20.121 |
| B1 | 66 | 30.221 | 4.223 | 27.273 | 61.844 | 3.270 |
| B2 | 66 | 27.988 | 4.320 | 24.290 | 56.776 | 3.199 |
| A2 | 65 | 28.007 | 59.817 | 36.521 | 124.986 | 19.827 |

64路由派发抽样均值59.817–61.782ms→4.223–4.320ms，线程CPU19.827–20.121→3.199–3.270ms，符合原健康租借64 PING+64 GET→1 PING+1 read-only EVAL的真实命令证明。claim27.988–30.221ms仍在，CPU和嵌套phase不得相加。

| case | 原任务类型 | 抽样数 | 入队至开始ms | durable get RPC ms | 执行总ms |
|---|---|---:|---:|---:|---:|
| A1 | peer receive | 161 | 20.311 | 4.779 | 5.012 |
| A1 | receiver ACK | 141 | 67.333 | 7.154 | 20.111 |
| B1 | peer receive | 123 | 6.788 | 4.899 | 5.107 |
| B1 | receiver ACK | 190 | 102.840 | 6.993 | 19.296 |
| B2 | peer receive | 128 | 8.135 | 4.893 | 5.030 |
| B2 | receiver ACK | 182 | 98.972 | 7.032 | 19.644 |
| A2 | peer receive | 168 | 22.260 | 4.890 | 5.108 |
| A2 | receiver ACK | 139 | 66.128 | 7.228 | 20.053 |

抽样限频且共享budget、包含warm，不能估P99或称所有任务都等待该值。peer排队下降，ACK排队ON98.972–102.840ms，OFF66.128–67.333ms；ACK仍每人原durable Get和Confirm，执行约19–20ms，共用原business_runtime.worker_threads=4，不能删读鉴权/ACK序号证据、盲增线程或降持久化。

A2最慢MID2895：sender ACK53.232604ms；网关B在发送后25.194ms开始领取48条，批耗115.764ms；网关A在发送后1060.946ms才开始领取剩51条，批耗128.808ms。前48人81.897–189.004ms到达，剩51人1091.127–1203.057ms；相邻收件空档902.123217ms。两批48+51=99，最终SQL全部状态3/attempt1。源码Run：processed不足64且partialOFF时进入1000ms恢复等待，即使SKIP LOCKED可能仍有可领取任务。时序符合这个错误空闲判断，低频日志未覆盖每次空claim和实际锁持有，不能声称完整锁因果已证明。上一partial单独对照未重现该长尾，旧结论保留；下一使用本轮同image和route1/completion1/claim1，仅partial OFF/ON/ON/OFF，核验有界继续领取及100人剩余peer/ACK拥塞。

真实原始结果stage group-route-batch-control-20261006和cross-feature-groute1featA1/B1/B2/A2完整保留。最终restore-summary状态ACCEPTED_GATEWAY_AND_MESSAGE_FULL_ENV_RESTORED：原5c2645 Gateway/e687 ELF、be8 Message/2548 ELF、全部Env/HostConfig/Health/Mounts恢复，其他16实例/配置/持久化1/1/1/0/0保持，自己的actors关闭，宿主其他应用保持。以该新receipt实例身份为准，旧CID是历史。候选尚未选入长期运行/万人混合，全功能目标OPEN。

本轮只读分析又有一次把报告错假设在benchmark/local_capacity的路径错误；随后rg定位docs/performance并读取。此前StorageWaitTiming路径假设错误亦由rg定位common/db。只读无运行/代码影响，保留工具输出及记录，后续先查路径。


后续3ba6007同路由开启镜像的partial独立ABBA也完成：528消息/23628确认/216全功能操作148断言正确，65/100仍FAIL，没有稳定平均收益且两模式均未重现902ms长尾。原运行已恢复；完整数据见GROUP_FANOUT_PARTIAL_DRAIN_20261006.md。后续方向为群peer/ACK遗漏既有message_executor隔离，而非反复归因Redis或盲扩线程。

本轮控制准备预执行发现新助手源receipt误写成独立partial-source，但源码包实际统一outcome-source；在运行前保存新助手预映像及旧tar包，创建v2包，仅修正receipt绑定，3ba6007提交后真实控制一次完整通过。刚才只读审查另有common/rpc及message application旧路径假设、PowerShell通配Literal rg路径错误、不存在tests/CMakeLists路径，均由rg --files/实际CMake定位，未改变源码/运行，记录便于复盘。
