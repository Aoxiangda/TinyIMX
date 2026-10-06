# Message同步RPC轮询profile对照（2026-10-07）

状态：准备可验证候选，尚未测量或接受。上一同调用Get约13ms、handler约2.5–2.7ms，约6.4ms在handler构造前、4ms在handler计时后；这些范围包含准入/传输/调度/诊断，不能认定唯一瓶颈。实际gRPC默认CQ1/min1/max2，而MessageServer未显式设置。TINYIMX_MESSAGE_RPC_POLLERS_ENABLE只精确1开启CQ1/min4/max8，其余不调用SetSyncServerOption，保持原librarydefault；不改existingMessageServer class/API/layout、oldmain/Proto/auth/lease/FIFO/ACK/query/durability/业务worker16或SQLpool16。MIN/MAX是同步轮询线程数，不是RPC总handler上限。可能增加CPU调度开销，需要实测。

native仅自有ephemeral127.0.0.1 fakeMessageService，无SQL/外部数据写入。覆盖absent/invalid/realENV、空service/地址/repeatedStart/readiness、16nativecaller各4次共64RPC身份内容一致、错误状态、Wait/Shutdown/重启fence；各case新进程，原native/日志保留。编译仅新Server.o和nativeTU，Message原密封链接替换Server对象，其余borrowed对象/包逐SHA核对，旧class header/meta/编译布局保持。新独立image不直接部署验收配置。

计划同新Gateway ac546和同新Message profile image，只开pollerflag0/1/1/0，固定traceUID519862/batch128/coalescing1/clientdeferred1/完整其他Env。四同自有群/100连接/528消息23628逐人wireSQL3、四54op37断言及文件字节，原3秒ACK/ALL和SQL8秒观察保持，不估P99/容量。分析同调用是否减少handler前等待与端到端ALL，同时观察其他大小/完整串联退化，不以平均或RPC指标改善掩盖tailFAIL。最后完整恢复原配置与19服务/other16/私有配置/宿主应用。全部功能1万到5万极致目标OPEN。

## 构建失败与修复（保留首轮）

首轮 `.local/codex/message-rpc-pollers-build-20261007`：Server TU 成功，native TU 失败，错误为 `message.grpc.pb.h` 不存在。实际封存的生成头路径是 `tinyimx/message/v1/message_service.grpc.pb.h`（与现有 Impl 一致）。修复 native include，在新 attempt2 目录重编译，保留原失败 JSON/编译日志。首轮未构建新镜像、未部署、未创建压测数据，runtime-after 完整校验与之前恢复收据相同。该错误属于测试依赖名称，不是运行时性能回归。

## 实际四轮结果与决策：不接受该轮询配置
构建源码 144268ae；镜像 Gateway ac546 / Message 5eeef2；21断言×5实际ENV=105 PASS，64并发调用×5=320（另各有1次非法参数RPC），readiness/stop/default/invalid均通过。原边界145测试同SHA继承，不冒称本轮重跑。builder只替换Server.o，其他依赖/原main/class header/proto借用逐SHA核对。
实际ABBA：A1 OFF、B1 ON、B2 ON、A2 OFF；同新镜像，Gateway/Message固定traceUID519862，batch128、coalescing1、deferred1、SQL pool16、两Gateway各16业务worker。原3秒ACK/ALL、SQL8秒观察不变。共528消息/23628逐收件人wire和最终SQL3、四全功能串联216操作148断言/四文件字节一致；0观察重复。每size/case 3warm+30正式，仅串行100连接，不是P99或容量认证。
|case|配置|2人 ALL mean/max ms|16人|65人|100人|
|---|---|---:|---:|---:|---:|
|A1|off|30.844/38.007|47.647/57.847|96.727/123.178|134.099/183.774|
|B1|on|30.531/44.118|47.547/61.607|101.293/129.416|134.829/196.205|
|B2|on|32.137/42.699|46.974/61.123|104.591/127.812|125.828/168.739|
|A2|off|30.498/38.638|48.281/59.506|101.301/161.401|130.522/184.713|

100人最慢ON对最快OFF的保守均值改善 **-3.30%**；65人 **-8.13%**，16人仅+0.21%，2人-5.38%。100人B2单轮125.83ms不能宣传稳定提升，B1仍134.83ms。65/100 max仍超过100ms。候选功能正确性通过但性能收益不通过；**不启用poller profile，原接受运行镜像5c2645/be8完整恢复，实验代码默认OFF保留供复盘**。同一四轮的常数诊断开启不能证明无诊断扰动，更不能外推全部功能1万到5万极致。

### 同调用边界核对
每case393条/131唯一键；4case共524个完整client/handler/repo配对，0捕获键未配对/重复/非成功/时钟嵌套错误。同kernel boot及单调offset0逐case核对。限频8/side/s且未设全RPC counter，因此不是全RPC全量覆盖。选择UID519862，多数任务kind未知，不臆断全部属于ACK或peer。
|case/100人|正式paired|gRPC mean|handler total|Acquire含PING|Query客户端|handler前|handler后|
|---|---:|---:|---:|---:|---:|---:|---:|
|A1|59|13.237|2.463|1.054|1.083|6.874|3.900|
|B1|60|13.681|3.616|1.808|1.357|6.132|3.933|
|B2|60|14.196|4.081|1.636|1.834|6.372|3.743|
|A2|59|13.357|2.530|0.958|1.235|6.509|4.318|

OFF gRPC13.237/13.357ms，ON13.681/14.196ms。handler前OFF6.874/6.509→ON6.132/6.372ms，只是小幅改变；handler total OFF2.463/2.530→ON3.616/4.081ms，Acquire与Query墙钟同步变长，handler CPU约0.54–0.61ms。**不能把RPC范围外等待直接叫纯网络，也不能把入口poller不够定为唯一根因**。增加入口轮询没有稳定缩短整RPC/最终ALL，后端竞争/调度仍为待验证原因；这里的观察不等于精确因果分解。

### 资源与测量限制
B区间4秒资源快照：MemAvailable 8843372 KiB约8.43GiB，memory PSI avg10/60/300=0；vmstat实时4点 idle57–70%、runqueue2–15、wa/st=0，swap-in最高16KiB/s、swap-out0（首行启动以来平均不用于此结论）。CPU PSI some avg10=15.14%；这说明有短区间调度等待，不足以归因宿主游戏或证明整个压测CPU状况。无内存清理/删文件/drop caches。
第一次资源probe的State.Pid为docker-init，Threads1不能当Message业务线程数；后续明确更正。CPU quota probe首次因SSH无权读取/proc/PID/root停止，只读失败审计保留；使用Docker exec cat只读fallback成功，无权限设置改动。fallback发生在原配置恢复后：Gateway真实进程62线程、Message32线程、MySQL87线程；各role cpu.max=max、cpuset0–7、nr_throttled0，完整HostConfig与测试时核对一致，因此不是容器配置CPU限额。计数为累计且恢复后快照，不能当ON线程数/全压测等待归因。

### 当前可靠方向
1. 先精确区分Get请求在gRPC传输/入口准入/唤醒与业务FIFO工作队列上的等待；CPU等待不能只看平均idle。当前只有handler边界，不能盲目继续加线程。
2. 群扇出仍按收件人进行durable Get和ACK确认，重复RPC/SQL往返与有序队列可能放大尾延迟。评估批量durable读取/减少重复读取及分条队列公平性，必须保持认证、租约、MID/UID匹配、FIFO、ACK顺序、最终SQL状态与重启恢复；不以缓存跳过验证或异步放弃持久确认。
3. 每轮先native与并发/非法/生命周期验证，再完整串联和同场景ABBA，再通过后才扩大到1万/2万/5万混合与长期/重连/故障。现有1万选择性混合通过不等于所有功能容量；2万延迟/5万/AI仍OPEN。

完整raw guest：.local/codex/message-rpc-pollers-control-20261007，分析message-rpc-pollers-analysis-20261007；前trace四轮/首次失败及修复均保留。全关联JSON保存benchmark/local_capacity/results/message_rpc_pollers_20261007.json。公网/私有配置/Env/凭据不纳入导出。
