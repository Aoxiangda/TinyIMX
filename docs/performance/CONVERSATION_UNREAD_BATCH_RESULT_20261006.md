# 会话未读批量优化结果与全功能问题复盘（2026-10-06）

## 结论及范围

源码、真实Redis语义验证、同ELF ABBA支持的瓶颈：50项会话逐项健康连接租借和GET，放大共享连接池、线程与网络往返成本。有界批量读取已在两个Gateway运行。指定10k混合负载列表P99至少降低80.46%，私聊持久化ACK P99至少降低18.12%。

采用最大ON P99与最小OFF P99保守计算，没有平均P99。两轮ON通过原门槛，但100ms只是既有门槛，**全部功能/50k活跃业务/AI/故障/长期稳态极致性能尚未验收**。

## 原因与实现

原HandleConversationListRequest对RPC授权的最多50项，每项GetPrivateUnread分别Acquire/健康PING/GET。新GetPrivateUnreadBatch硬限50，同一原健康租借后一次Lua逐项pcall GET，返回标签和原bulk string。50项服务器GET仍50次，网络命令100→2、租借50→1。

复用原from_chars，禁止Lua tonumber；保持INT64_MAX、大于2^53、invalid/missing/wrongtype、非法值/负数/溢出/NUL、重复键、顺序、count/错误文字。空/all-invalid/超过50无Redis I/O。回复整体长度/标签先校验；不写key/续TTL/DEL/扫描/自动重试，不用MGET的nil混淆wrongtype。

Gateway只读已授权peer，原actor/分页/排序/metadata/limit/取消/每项零fallback保留。环境变量严格等于1才开启，代码默认OFF。密码验证、健康PING、身份/持久化ACK、durability均不变。Lua占用Redis单线程，因此有界50；当前standalone，不声称Cluster跨slot支持。快照不保证SQL与Redis跨存储原子一致。[Redis官方原子执行与pcall说明](https://redis.io/docs/latest/develop/programmability/eval-intro/)。

## 实际Redis与native验证

pool1/4各124检查，共248 PASS：逐项状态/error/count，50/51/空/null/未初始化/自有池shutdown，精确int64，100并发mixed，200原子双计数快照，所有租借归还，原bytes/TTL不变。仅新自有namespace key，TTL300，不清生产键。

同native ELF，每case100页×50项ABBA：

| Pool | 原逐项两轮mean/P99 ms | 批量两轮mean/P99 ms |
|---|---|---|
| 1 | 23.981/45.100；28.839/50.047 | 1.264/2.584；1.188/2.619 |
| 4 | 26.392/47.258；27.278/48.455 | 1.781/3.124；1.455/2.933 |

每case5000个count全正确，租借/PING/读取命令各5000→100，服务器GET仍5000。全部检查和每页时间保存。closed-loop组件成绩不是RPS/万人服务容量。

## 真实固定50项列表ABBA

同Gateway ELF OFF→ON→ON→OFF；其他17实例/原配置/durability固定。单持久TCP actor，每轮10预热+100测量，400完整响应字段/顺序/未读全相等，0错误/超时。limit1/20/0/100、负数/字符串/小数、actor注入保留原行为。

| case | mean ms | P50 ms | P99 ms | max ms |
|---|---|---|---|---|
| single-A1 / off | 39.553 | 39.462 | 62.339 | 69.510 |
| batch-B1 / on | 9.539 | 9.347 | 14.563 | 14.945 |
| batch-B2 / on | 9.253 | 8.687 | 14.809 | 15.646 |
| single-A2 / off | 34.556 | 33.958 | 57.158 | 66.513 |

该阶段无ring压力，是单用户串行样本，不是开环吞吐。固定自有50关系/消息经正常接口创建并实际接收ACK，业务数据保留，无删消息/清未读。

## 10k同步混合ABBA

每轮10000原自有账号长连接，总登录100/s；私聊开环100/s×60秒=6000planned；同原monotonic barrier，单自有actor固定50项列表20/s×60秒=1200planned；稳态另4名新自有用户49操作链。

原native worker SHA 5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3；原coordinator SHA 1ef1c527335a998809296d487378776715ab0fe8c73ccda90d8fa4392431d8ba。原timeout/drain/SQL/Pong判定不变，所有scheduled/send/response/失败逐条保存，无跳过/延后窗口/同期AI推理。

| case | 私聊ACK P99上界 ms | scheduled→ACK P99上界 ms | 会话mean ms | 会话P99 ms | 会话scheduled→response P99 ms |
|---|---|---|---|---|---|
| mixed-A1 / off | 93.9 | 97.9 | 97.070 | 246.504 | 246.950 |
| mixed-B1 / on | 59.4 | 62.1 | 16.445 | 38.092 | 38.994 |
| mixed-B2 / on | 70.5 | 72.8 | 16.746 | 45.531 | 46.465 |
| mixed-A2 / off | 86.1 | 88.0 | 96.340 | 232.961 | 233.466 |

四轮原私聊门槛全部PASS：各10000登录，0意外断连，全部已发心跳drain；6000正ACK=SQL持久行=确认，收发身份/MID/wire投递逐条一致，0负ACK/未决/跳过。总24000私聊、4800页全部正确；196公开链操作/144断言PASS，都在原稳态内。

OFF A1列表发送迟到P99仅1.902ms，响应P99 246.504ms，差异不能仅归因未及时发送。客户端采样仍含JSON/证据处理；固定客户端同ELF对照支持批量收益，不能独立精确拆CPU/磁盘/网络份额。

10k连接不等于10k请求/s，仅私聊100/s、列表20/s、低样本交互链。此前10k150/s持久化ACK批量对照仍失败，不把不同mix的本轮PASS写成那个点已修复。

## 全功能交互及慢操作

28公开请求、真实私聊/群聊ACK/Pong；好友创建/列表/接受/拒绝、资料、私聊幂等/历史/已读/未读衔接、全部群管理/三人群投递/禁言越权拒绝，文件三控制/跨所有者拒绝/幂等取消，真实1.8MiB文件续传下载SHA字节核对。33入口逐项证据见 [完整清单](ALL_FEATURE_OPTIMIZATION_20261006.md)。

| 操作链 | operations | checks | 最慢单次ms | 最慢两项 |
|---|---|---|---|---|
| cuall20261006 | 49 | 35 | 150.503 | file-begin 150.503ms; group-create 134.176ms |
| cumixfeatA1 | 49 | 36 | 112.107 | file-begin 112.107ms; group-leave 66.470ms |
| cumixfeatB1 | 49 | 36 | 42.855 | group-create 42.855ms; group-leave 36.990ms |
| cumixfeatB2 | 49 | 36 | 38.596 | login 38.596ms; login 33.617ms |
| cumixfeatA2 | 49 | 36 | 60.411 | private-idempotent-repeat 60.411ms; conversations-before-read 54.593ms |

file-begin150.503ms、group-create134.176ms、混合OFF A1 file-begin112.107ms都保留。这些是低样本单次，不是P99，也不足以直接归因数据库。好友列表已有单JOIN，群列表已有有界游标查询，群成员列表还包含群/成员授权读取与一致性事务，不把全部功能套用逐项Redis根因。慢写操作需关联阶段后再改。

## AI/MCP独立问题

Ubuntu已有Ollama0.18.3监听127.0.0.1:11434，qwen2.5:7b已安装；原profile host.docker.internal桥接不可达且qwen3:8b不存在。本轮仅新私有测试配置修正端点/模型，原19实例/原配置未改，不能称原生产profile已修复。

首次真实模型调用30.521秒FAIL，原provider单HTTP期限30000ms。日志显示tensor loading未完成，客户端关闭导致loader abort，/api/ps仍空。CPU model buffer4460.45MiB、mmap=false。可确定冷加载未就绪被业务期限取消，尚不能唯一归因纯磁盘或某个宿主进程。

按 [Ollama官方空请求预加载方法](https://docs.ollama.com/faq)，独立startup readiness空prompt /api/generate，启动操作deadline90秒，实际85.070秒HTTP200就绪。**单个模型业务HTTP仍30000ms**。随后2个真实Agent分别43.313/19.928秒，各1轮1次资料tool、正确自有UID/用户名。Agent包含工具前后两次模型HTTP；43秒不等于放宽单HTTP期限。实际GGUF7.6B/Q4_K_M、size_vram0（CPU）、context4096、驻留约4.605GB。

两轮各8域MCP工具及2鉴权/actor注入负例PASS。临时AI/MCP容器按精确ID停止保留；原模型服务/安装/全局配置不改，模型原5分钟自然过期。2成功例仅证明真实链可用，**AI仍秒级，无P99/并发容量验收**。后续拆就绪/每轮推理/工具RPC/总Agent阶段，并完善正确的项目启动入口。

## 失败与解决方法

| 问题 | 证据/原因 | 修正及记录 |
|---|---|---|
| Redis预检首次AUTH失败 | 工具凭据来源错误 | 新attempt2仅RAM读取原配置独立RESP，原失败保留，无产品/Redis改动 |
| 首次隔离构建FAIL | UnreadCountCache对象属于libtinyimx_cache_service.a，原脚本断言libtinyimx_cache.a | 新attempt2按真实成员修复，原失败object/failed/script保存，原库未覆盖 |
| 会话逐项往返放大 | 源码/native/真实混合ABBA | 有界只读批量、原语义不变、开关可恢复 |
| 原AI地址/模型错误 | loopback tags200、bridge不可达、无qwen3 | 仅新测试私有profile正确；原生产入口待改 |
| AI冷加载取消 | 30.521秒+loader abort日志 | 独立就绪85.070秒；2真tool成功，业务deadline不改，仍慢 |
| 其他慢写操作 | 单次file-begin150.503/group-create134.176ms | 原操作全量保留，先阶段分析，无低样本P99结论 |

## Git、运行和恢复

逐次源码/测试迭代：

- 3ba4f7c373f6c438cea44329ff12ea90679773b6 feat(cache): add bounded atomic conversation unread reads with real Redis conformance
- 414862d735404509f24e3b10fe15feb50f7289eb perf(gateway): batch conversation unread snapshot behind default-off control
- 232a172b41a825ede81b9886e5c410bacf4e6734 fix(perf): target cache-service archive in isolated unread candidate build
- d9c2596ebe630cc3892e533d55835c2d29e4201f test(perf): preserve audited conversation endpoint ABBA and feature chain controls
- 34fa7545aef16fa9c7843f9e174cfbb887074a57 test(perf): add synchronized 10k private conversation and feature-chain controls
- 1f6c5ca3be3d5c2e979372ef2fc0cf0b9c22abc6 test(perf): preserve scoped rollout and real Ollama MCP verification
- 6ad31d7aec940674b4042e6c7a1912ae23f9d2c6 test(ai): diagnose cold-load cancellation and measure separate model readiness

当前Gateway image sha256:a2bb65215bfc85246b946a8c0850e134cd3144d078e5b56c376676c2cd8d1cfa；ELF ca01b00011b29eff29fe776fe3ac2dbaa0b4bb6e7a4ddce76412b82e583579ce；两个网关显式TINYIMX_CONVERSATION_UNREAD_BATCH_ENABLE=1。原库/ELF/镜像保留，其他部署代码默认OFF。基础compose单独启动可能丢失当前private override，应按scoped acceptance记录使用。

完整原Gateway image/Env/Cmd恢复配置保存在 .local/codex/accepted-conversation-unread-20261006/runtime-private/restore.override.json，含私有环境，内容不提交/导出。相同生产compose/环境文件，仅--no-deps --pull never gateway-a gateway-b；先审计无外部活跃连接/其他17身份。当前接受版本5完整页+登录/Pong烟测PASS。

原私有配置SHA、其他17 ID/image/StartedAt、其余Env/Cmd/HostConfig/mounts保持，19健康，SQL durability1/1/1/0/0；无KDF降级/ACK提前/健康检查移除/系统时钟或虚拟化改动。宿主游戏/其他应用保留，无全局缓存/swap清空或业务删除。

## 证据索引与后续

仓库.local/codex原证据至少：conversation-unread-batch-api-20261006、conversation-unread-batch-build-20261006（FAIL）及-attempt2、conversation-unread-batch-gateway-run-20261006、conversation-unread-mixed-10k-20261006、capacity-cumixA1/B1/B2/A2、cross-feature-cuall20261006/cumixfeatA1/B1/B2/A2、accepted-conversation-unread-20261006、allfeature-real-ollama-20261006（FAIL）及-attempt2、real-ollama-timeout-analysis-20261006、conversation-unread-result-analysis-20261006。

公开原日志/逐请求/对账导出带SHA manifest；私有配置/token/inspect/binary/payload留guest，导出副本按敏感字段脱敏，原证据不删除。

下一点20k长连接+135/s私聊+20/s固定50项列表+4人49链，原工具不改，仅新增独立20k调度。单ON点不做ABBA归因。保留全部计划请求/失败/原100/s登录爬坡。20k/30k/50k每类功能及TLS/离线/热点/过载/soak仍NOT_RUN，失败按首窗口关联证据后改。

## 后续20k实测已保存

20k长连接+135私聊/s+20会话页/s+公开交互链已完成。8100私聊/1200完整页/49操作36断言正确，231524发出心跳全部响应；延迟FAIL，原ACK P99上界201.0ms，会话页P99 155.029ms。未放宽期限/漏掉失败。相同MID阶段分析及准确的下一步见 [20k失败复盘](MIXED20K_BOTTLENECK_AND_NEXT_20261006.md)。以上“下一点20k”是先前准备时记录，当前状态以本段和新复盘为准。
