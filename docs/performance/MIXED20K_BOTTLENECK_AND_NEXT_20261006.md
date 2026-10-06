# 20k混合失败复盘与下一瓶颈（2026-10-06）

## 结论

当前接受的会话批量版本在指定20k混合点上正确性通过、延迟FAIL。**不能把10k收益外推为20k/50k全功能极致性能。** 20k没有代码回退/删除数据/改deadline/弱化持久化；原19实例与原配置完全保留。先分析首个失败窗口再迭代，不重测相同方案。

源Git3675493ce31c10923cfad4f6923624ff2036a1b9；网关同已接受a2bb65215bfc85246b946a8c0850e134cd3144d078e5b56c376676c2cd8d1cfa、会话批量开关1，Message原be8b...和durability1/1/1/0/0不变。

## 测量条件与完整正确性

20000原自有用户700001..720000，总登录爬坡100/s；原native worker/coordinator SHA不变。双worker各10000连接/50次登录每秒。原60秒steady barrier私聊135/s=8100planned；同barrier单独固定50项列表actor20/s=1200planned；4新自有功能actor完成49项操作/36断言，全部在原稳态内。原100ms gates、drain、SQL/wire确认/原deadline保留，无同期AI推理/构建/清理。

20000全部登录，0意外断连；231524个已发心跳全响应。8100send=8100正ACK=8100SQL=8100confirmed=8100实际wire投递，0负ACK/未决/身份差异/跳过/未知SQL；1200完整列表响应完全一致，0错误/超时/跳过。仅私聊正ACK和scheduled→ACK P99 gates失败，会话1200功能PASS也不等于性能PASS。

| 指标 | 结果 |
|---|---|
| 原私聊ACK P99直方图上界 | 201.0ms，FAIL |
| 原scheduled→ACK P99直方图上界 | 203.4ms，FAIL |
| 原私聊ACK最大值 | 341.465ms |
| 8100精确ledger send→ACK mean/P50/P99/max | 54.517 /42.038 /200.866 /341.438ms（诊断时间，不替换原gate） |
| 会话sent→response mean/P50/P99/max | 37.962 /29.734 /155.029 /214.899ms |
| 会话scheduled→response P99 | 155.692ms |
| 会话发送迟到P99/max | 2.031 /4.112ms，不能解释155ms响应尾部 |

这比10k多连接也多私聊速率，是新负载点，不能用它与10k之间的差值证明某次修复导致回退。官方原直方图门槛与每个失败请求/慢操作全部保存。

## 精确身份关联的阶段证据

从2个worker原7列ledger，逐条MID、sender、receiver、ACK seq及client_message_id连接原SQL对账。实际wire协议可缺省client_message_id；本轮8100条均未携带该字段，通过MID/from/to映射原正ACK的CID及SQL，保持原严格身份规则。辅助分析第一版错误要求wire CID必须存在并停止，后续attempt2按实际协议更正，原失败助手/审计保留，原压测代码与判定未变。

原运行Gateway有228条阈值/限频阶段样本，仓库488条，精确同MID交集19条；未启用的handler/pool CPU诊断0条，**0条不是0耗时**。以下只报告19条慢样本mean/max，不把这个交集当总体P99或CPU/磁盘唯一根因：

| 阶段 | 样本 | mean ms | max ms |
|---|---|---|---|
| acquire_ms | 19 | 5.965 | 31.947 |
| after_repository_ms | 19 | 52.214 | 130.164 |
| batch_wall_ms | 19 | 7.332 | 20.802 |
| before_repository_ms | 19 | 64.905 | 144.589 |
| commit_ms | 19 | 23.791 | 51.094 |
| dispatch_age_ms | 19 | 2.243 | 15.996 |
| gateway_persist_rpc_ms | 19 | 76.714 | 118.773 |
| outbox_ms | 19 | 7.524 | 26.533 |
| permission_ms | 19 | 19.241 | 60.685 |
| precheck_ms | 19 | 2.198 | 5.232 |
| repository_ms | 19 | 47.998 | 93.109 |
| route_ms | 19 | 14.049 | 82.821 |
| rpc_minus_repository_ms | 19 | 28.715 | 53.768 |
| send_to_ack_ms | 19 | 165.118 | 224.150 |
| unread_ms | 19 | 29.611 | 103.034 |

这19条平均send→ACK165.118ms，仓库47.998ms（其中commit23.791ms），仓库之前64.905ms、之后52.214ms。before+repo+after逐请求可加，但内部permission/route/RPC/repo等阶段有嵌套，不能将表所有行相加。Gateway dispatch_age仅2.243ms平均，不足以断言一切是业务队列；persist RPC与仓库差额28.715ms包含应用/传输/调度等，不能当“纯gRPCCPU”。unread平均29.611ms也提示需区分Redis租借、健康PING、命令处理和调度。

原代码审查：Redis Acquire先锁/等slot，解锁后保留PING，再执行请求；presence维护、路由、未读共享池的竞争是待验证假设。旧在线维护固定4worker/16batch/5ms方案曾有ABBA，没有依据直接重用或改成“线程越多越快”。下一步需仅开启有界低频诊断，将等待/健康PING/命令阶段分别保留，再选可证明的改动。

## 全功能低样本交互

49操作/36断言PASS，28公开类型与私聊/群聊接收ACK/Pong、真实1.8MiB文件字节/SHA核对正确。最慢项仍只是一轮低样本，不是每接口P99：

| 操作 | request type | 单次ms |
|---|---|---|
| group-unmute | 2041 | 95.537 |
| group-create | 2023 | 90.903 |
| group-leave | 2033 | 86.870 |
| file-begin | 2053 | 79.653 |
| muted-group-send-denied | 2049 | 75.514 |
| group-send-with-fanout | 2049 | 58.525 |
| login | 1001 | 58.189 |
| private-send-after-friend-accept | 2001 | 54.448 |

群成员列表并非“整体单SQL”：需要群与actor授权读取、页面SQL、一致性事务commit。文件/群创建有幂等预读、身份检查、多条元数据/成员/历史/Outbox写和commit，业务必要语义不能删掉换成绩。源码显示不存在会话页每项Redis GET同类N+1，不能套用同一方案。新群/关系/消息/文件记录全部保留。

## 资源判断

原54个资源快照覆盖登录爬坡与steady，不能将全段DockerCPU均值当steady或单请求CPU。guest最低MemAvailable=8561228KiB，约8.165GiB；内存PSI样本未见压力。宿主只读20k记录约3.8GiB可用，其他应用/游戏保留，无清理/停止/系统设置变更。现有证据不支持清内存就能修复此点。

CPU PSI代表runnable stall比例，不是CPU利用率；steal0也不排除VMware/宿主调度影响。当前数据还不能唯一归因物理磁盘、虚拟化或某个进程。继续限定同一稳态窗口和阶段诊断，不能随意迁移/删文件/关闭宿主功能。

## AI真正慢在哪里

只读Ollama历史GIN日志，按已停止自有AI容器准确StartedAt/FinishedAt关联，每例恰好2个POST /v1/chat/completions并均HTTP200：

| case | 整个Agent秒 | 两个模型server HTTP秒 | 两HTTP时间占整Agent |
|---|---|---|---|
| ready1 | 43.313 | 28.848 / 13.649 | 98.12% |
| ready2 | 19.928 | 3.956 / 15.501 | 97.63% |

GIN server时间包含推理/请求处理，非纯CPU或token吞吐。两个模型HTTP合计占97.63%–98.12%，足以判断这2例秒级耗时主要位于模型服务，不是资料tool RPC。冷加载独立85.070s，原30秒单HTTP期限保持；生产原AI profile桥接/model仍需正确启动入口。没有AI容量/P99/100ms达标结论。

历史日志解析首版因guestPython对-0700时区格式不同失败；attempt2仅规范时区冒号与纳秒位数，原日志/失败保留，不改产品模型参数。

## Git、审计和证据

20k压测/helper源已3675493保存；完整10k代码与七次迭代见前报告。本报告与分析两版、AI阶段两版、公开证据导出助手均另次提交。原三处既有dirty文件与.env.example逐SHA保留，不stage/reset。所有服务/镜像/私有配置/HostConfig/mounts保持，代码和每个失败均可复盘。

原文件：.local/codex/conversation-unread-mixed20k-20261006、capacity-cumix20kON、cross-feature-cumixfeat20kON、conversation-unread-mixed20k-analysis-20261006（失败）及-attempt2、real-ollama-phase-analysis-20261006（失败）及-attempt2。完整raw日志留runtime-private；公开numeric/逐请求/SQL对账导出SHA清单，仅导出副本脱敏，不动原证据。

## 未完成与下一次迭代准则

20k原延迟FAIL，10k/20k/30k/50k全部功能极致目标仍未完成。下一步定位Redis pool slot/健康PING/命令处理和post-repository阶段，保留原SQLcommit/身份/ACK；针对文件/群写操作独立阶段分析，AI按模型就绪与每次推理分别计量并修复项目启动入口。每个候选先真实正确性、同ELF对照和交互回归，再按原门槛接受或恢复。失败不丢弃、不放宽deadline、不直接盲冲50k。
