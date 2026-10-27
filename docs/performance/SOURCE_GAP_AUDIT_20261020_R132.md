# TinyIMX 与固定 WuKongIM：源码差异、实测和取舍

## 当前结论：R135完整native实测通过，仍未达到目标

**这不是主要由epoll使用方式解释的差距。** 已核实的实现差异在权限/身份SQL、持久化、接收确认、后台派生和业务排队。此前减小RPC往返、换调度方式的优化保留了绝大部分每消息工作，收益有限。新的本地持久化候选已接入真实Message并完成下述正式点；原20服务保持原SQL版本，候选没有默认推广。

最新审计重新核对两边16个冻结源码文件的SHA和函数行锚，记录当前10个入口，形成8项差异/动作/下一验证：[R135逐文件审计](../../benchmark/results/peer-20261017/PERF034-current-full-source-gap-r135-r1/result.json)。这是**当前候选实际差异**，下方旧轮待办描述须按本节已完成状态理解。

|问题ID|WuKongIM固定源码|TinyIMX当前差异|已做改动 / 下一验证|
|---|---|---|---|
|PERF-099 权限|本地owner直接读权限，远端owner才RPC|进程内权限端口虽去Social RPC，仍进工作池、等future、每消息查权威好友SQL|旧完整对照ACK尾观察改善18.37%/5.24%；未有本地权威权限KV。下一步当前权限SQL/队列和撤权失效验证|
|PERF-055/096/102/105 持久化|AppendMessages→就绪聚合最多100批→pebble.Sync→完成通知，无人为凑批计时器|默认SQL消息/Outbox事务链仍重；native已接入本地原子消息/事件/索引批次，群聊仍SQL|本轮真实双Gateway/Message/projector/MQ/Redis完整42k通过。下一步新Peer/反向及真实写组；Write调用数不等于物理fsync数|
|PERF-105.5 身份桥|自身本地键无Tiny旧SQL比较器|native每条仍租SQL连接/PING，查WEIGHT_STRING和双方EXISTS|旧暖身份桥均0.783ms，含Acquire/PING0.399ms、Query0.375ms；仍是混合链。先验证大小写/重音/PAD SPACE及FK生命周期，再消融本地规范化/安全缓存|
|PERF-121/125 冷登记|没有Tiny的SQL外键引用登记|旧全局actor锁包SQL；现同actor协调，SQL期间释放全局锁|冷对照观察改善约59%，暖收益不稳。本轮显式ON；须正式反向及用户删除/owner失联验证，不把组合收益全部归到它|
|PERF-126/130 准入共锁|所核对发送→本地提交无Tiny发布器Stats门槛|旧Healthy→Stats与后台扫描/解析/入队共mutex，准入等待无关后台|已加原子健康状态，在原启动/失败/停机临界区更新。有限四轮ACK尾改善28.85%/16.59%，R30补分配失败fencing。本轮healthON；待正式反向及broker故障窗口|
|PERF-054 接收确认|普通Recvack主要移除内存重试，命令/master另更新会话|Tiny保留逐消息持久接收状态及待投递恢复索引，native仍同步写|已有受限批确认接口；旧SQL计时合批少约81% UPDATE却无稳定端到端收益。下一步确认写组/锁等待，乱序/空洞/崩溃验证后借鉴就绪合批|
|PERF-062/083 后台派生|进程内事件推进|Tiny仍有外部MQ producer/broker/proxy、独立projector、owner快照RPC、Redis及逐receipt SDK确认|本轮42000事件逐字节匹配、20000Redis计数正确、尾部排空，但仍5.441忙核。下一步当前CPU/SQL/SDK归属；旧56.59%不可当本轮拆分，暂停后台不算产品提速|
|PERF-087/093/094 排队|频道processing维持有序，存储聚合在另一层|用户/会话FIFO包长RPC链；释放OS线程不消除逻辑前驱|旧ready4/精确key无稳定收益，保持OFF；本轮Gateway asyncOFF。下一步同负载共同MID队列/存储等待交叉验证后改调度|

### R135：10000连接，700条/s，128B，60s

新空私聊历史、独立MQ/Redis；全部10000连接参与、两个Gateway各5000连接、5000对本地放置，12项已知阶段诊断关闭。Message为R30 ELF，native/pins/health均ON；这是**组合候选的一轮**，不是单开关因果消融。

|指标|本轮完整结果|
|---|---:|
|计划/发送/持久ACK/唯一投递/完整成功|42000 / 42000 / 42000 / 42000 / 42000|
|负ACK / 超时 / 跳过 / 重复投递|0 / 0 / 0 / 0|
|入队→ACK均值 / P99|6.1748 / **18.8822 ms**|
|发送计划→接收投递P99|**19.063715 ms**|
|整VM平均忙核|**5.4411**，保守对齐窗口59.1227s|
|客户端入队→Write调用 均值 / P99|0.02210 / 0.0539 ms|
|Windows最低可用内存|6.0508 GiB|

客户端入队→Write调用不是主要等待。Write调用→ACK仍含socket写和完整服务/返回链，不能全标作服务端处理。计划→入队有1078条提前唤醒，最早0.470842ms；R8一次Sleep后未重查due，原始有符号值保留。Windows页面读入最高12923.6pages/s，未证明来自pagefile；PERF-019/070继续比较计时/预热与分页来源。来宾无swap进出，不能因此忽略宿主干扰。

关闭后独立只读存储且SHA/mtime不变：42000个M/I/K/H/A一一核对，N/D/C/U/R全映射、10000个SQL actor引用一致，全部confirmed，E/F/P/Q为0；CID、内容及双方顺序一致。真实broker逐条读取42000事件，与A档案body逐字节一致，无重复；5个消费尾快照diff/inflight均0，Redis20000计数全部一致，未重放、重置offset或补写。

结果：[完整工作点](../../benchmark/results/peer-20261017/perf134-native-10k700-r2/point-result.json)、[全量客户端分解](../../benchmark/results/peer-20261017/perf134-native-10k700-r2/client-phase-costs.json)、[资源](../../benchmark/results/peer-20261017/perf134-native-10k700-r2/operating-point-resources.json)、[存储索引](../../benchmark/results/peer-20261017/PERF133-fresh-native-closure-r135-r2/result.json)、[真实broker和Redis](../../benchmark/results/peer-20261017/PERF133-exact-native-projection-r135-r1/result.json)。

### 新固定Peer R24已经完成，差距仍然明确

两边均为8核/16GiB原VM、原生Windows发压、10000全部参与连接、700条/s、128B、60s、新空私聊历史。每个系统分别运行，原服务在Peer轮暂时停止并完全恢复。Peer普通认证、持久消息、白名单及SDK加密均开启，无阶段诊断。

|本轮指标|TinyIMX native组合R135|固定WuKongIM R24|Tiny/Peer观察比值|
|---|---:|---:|---:|
|完整成功|42000 / 42000|42000 / 42000|—|
|ACK均值|6.1748ms|1.59195ms|约3.88倍|
|ACK P99|18.8822ms|12.9549ms|1.458倍|
|计划→投递P99|19.063715ms|14.145358ms|1.348倍|
|整VM平均忙核|5.44115|0.95157|5.718倍|
|保守对齐资源窗口|59.1227s|59.0830s|—|

Peer全部5000会话、42000 CID/MID/发送者/明文内容经历史接口逐条核对，0不一致；原20服务、全部2050容器配置/镜像、SQL设置、九个保留库、binlog、旧新native文件均保持。比较入口：[新鲜完整对照](../../benchmark/results/peer-20261017/PERF133-fresh-native-peer-comparison-r136-r1/result.json)。它是两个完整工作点，不是单变量因果ABBA或容量结论；接收确认持久语义/协议仍不同。Peer宿主最低可用6.574GiB，页面读入均27.57/s；Tiny对应均2181.71/s，环境差别继续跟踪，不能用它替代源码归因。

**PERF-137新增布局差异**：重新核对固定源ZIP及options→server→cluster→DB参数传递，实际R24为8个消息DB和8个slot-log DB；Tiny native为一个RocksDB。两边每库memtable均16MiB，但默认布局/后台任务并不相同。已准备固定ELF、仅改变消息DB分片的8/1/1/8新空历史消融工具，尚未执行，不据此声称分片是主因。slot-log与消息DB是不同路径，不能混为一谈；Tiny消息/事件/未读索引的原子性也不能靠改常量自动保留。[原始源码和实际目录](../../benchmark/results/peer-20261017/PERF137-peer-native-storage-layout-r1/result.json)。

PERF-136空间验证：新轮在创建10000用户后，逻辑数据20994180B，分配4138901504B，大块分配来自cluster/logdb的日志文件。这个新轮复现了“逻辑文件小但实际占用大”的机制；R23当时未采逐文件blocks，不能回填其缺失数据。空间维护后R24门槛及完整试验均通过。

历史SQL正式点38.545–44.599ms继续留作旧轮参考。新结果证明候选已进入完整业务并降低了时延，但CPU成本仍远高于Peer，最终对标目标未达到。下一步须当前native同负载低采样CPU/SQL/SDK分解、单变量消融及反向重复，然后容量、长稳和真实broker故障窗口验证。

失败保留：PERF-134首次空QPC屏障只有10000认证、0发流，经存储/SQL/broker证实；换新R135修正后42000实际闭环。PERF-135首次只读审计把10000 actor引用写stderr，父进程未并发读取导致回压；仅终止自有SSH观察器，保留失败，改stderr直接落本机文件，重读同一关闭库全量通过，未重启/重放业务。

空间维护：1248份已结束旧实验日志/ledger约512.9MB转存E、独立恢复核验后清除Guest重复副本。全部2050容器、源码/配置/SDK、数据库、binlog、快照保留。8核/16GiB正常重启后原20服务、数据/设置和旧新native文件重验；Docker Dns的null/[]序列化差别单独记录，其余配置仍严格比较。

---

本次核对的是 WuKongIM `a888f89533d0e7d1b2030e06504ca97f1ad891d4`，TinyIMX 复盘基线 `a222c8fd2d90b40ce308aea58c3c625666284900`。原 SQL 完整系统、可选 native 存储和每轮 ELF 分开记账。本页不宣称已经追平。

## 已知性能差距及其适用范围

原 SQL R12 完整四轮：10000 认证连接、128 B、700 条/s、60 s，每轮42000条全部正确。ACK P99 为42.7668/38.5450/41.1990/44.5990 ms，整机平均5.87–5.93忙核。固定 Peer 同负载独立 QPC 参考为12.5513 ms、约0.952忙核。最后一次持久化请求合包只降低两对尾延迟9.87%/7.62%，不足以追平。早期4.84 ms属于500/s，不能混入700/s比较。

原始结果：[完整R12对照](../../benchmark/results/peer-20261017/PERF102-full-abba-comparison-r9/result.json)。这组结果没有证明两系统或该硬件的容量上限。

## 从函数到实际工作量

|问题|Peer 固定源码|TinyIMX 差异与实测|已经执行 / 下一验证|
|---|---|---|---|
|PERF-099 权限|`internal/service/permission.go:216 requestAllowSend`，本地所有者直接`allowSend`；后者读Store黑白名单，非本地才RPC|原Gateway每消息Social RPC、双向好友SQL。受控本地端口消掉RPC，仍查权威SQL；没有实现Peer本地KV所有权|完整两对ACK P99改善18.37%/5.24%。保留逐条撤权检查；本地权限数据及失效协议仍待实现和验证|
|PERF-055/096/102 持久化|`pkg/wkdb/message.go:17 AppendMessages`写消息/序号并`CommitWait`；`wukongdb.go:413 loop`就绪聚合最多100批、`executeBatch`执行`pebble.Sync`|原Message RPC→租连接/PING→CID预查→事务消息写入/完整回读→Outbox→COMMIT。合包减少往返，不减少事务工作。native候选已改原子本地消息/事件/索引批次|SQL合包完整收益有限；本地存储组件对照和协议接入已经执行，尚缺无诊断完整10k固定负载与Peer新对照。不能把一次Write调用等同独立物理fsync，应测引擎真实写组与同步次数|
|PERF-054 接收确认|`internal/user/handler/event_recvack.go:16 recvack`，普通持久消息主要移除内存重试；命令/master另异步更新会话位置|Tiny普通确认额外持久更新SQL；native候选仍以同步WriteBatch保存状态/待投递计数。额外持久写仍存在|旧5ms确认合批少约81.33% UPDATE，端到端P99未稳定改善，未推广。不能直接删持久状态改变恢复合同；后续确认合批/游标需要乱序、空洞、重启验证|
|PERF-062/083 后台派生|`internal/channel/handler/event_persist.go`持久后推进进程内事件，`event_onsend.go:8`顺序执行permission/persist/sendack|Tiny原Outbox领取/续租/发布回写、外部MQ、SQL未读快照、Redis投影；旧轮MySQL+broker/proxy约占整VM忙核56.59%，该比例不是单消息时延|native候选改本地原子事件、有限发布器和owner快照RPC，真实MQ/Redis保留。当前33448私聊+21群事件全部broker/投影核对通过；实际broker成功后进度落盘前崩溃窗口仍待验证|
|PERF-087/093/094 排队|Peer频道processing约束同步onSend；存储分片合批是另一层|Tiny用户对FIFO覆盖较长RPC链，hash条带可能串行无关任务。释放线程不等于释放逻辑FIFO|旧ready4排队变长、精确Gateway键亦未证明尾延迟收益，保持关闭。当前native暖路径58共同MID网关排队均5.182ms；下一轮须把队列变化与持久化变化关联|
|PERF-105.5 CID兼容桥|Peer自身本地键模型，不需要调用Tiny旧SQL唯一索引比较器|native仍逐消息租SQL连接/PING，查询`WEIGHT_STRING`及双方存在性，以保留unicode_ci/PAD SPACE幂等合同|本轮共同MID身份桥均0.783ms，含租/PING0.399ms、查询0.375ms。不能称为纯本地链。去除此桥需明确兼容迁移/用户生命周期；随意改字节比较会改变大小写/重音/尾空格冲突行为|
|PERF-121/125 冷登记|Peer不经过Tiny的SQL外键引用登记|native原全局actor_mutex内租连接/PING/INSERT，使无关冷用户串行|已实现有界in-flight登记、同actor等待、SQL期间释放全局锁。真实行锁合同、18次终止恢复和同ELF ON/OFF/OFF/ON通过；冷ACK P99两对改善59.16%/59.76%，暖尾收益不稳定，默认关闭。这不是原SQL稳态差距的完整解释|
|PERF-126/130 健康检查共锁|所核对的onSend→本地AppendMessages路径没有Tiny外部发布器Stats门槛|`NativeMessageRuntime`准入调用`relay.Healthy→Stats`；与`LocalEventRelay::Poll`扫描/解析/排队共mutex。真实暖样本平均等待2.004ms|新增默认关闭的原子健康读取候选，保持状态在原启动/失败/停机临界区更新，后台扫描/发布/重试/持久化边界不改。必须先通过普通/sanitizer、阻塞扫描/失败/停机合同，再同ELF反向对照决定|

固定源码原文、SHA及函数锚点：[逐文件证据](../../benchmark/results/peer-20261017/PERF034-actual-r11-source-differences-r98/result.json)。当前实现入口：[本地提交](../../services/message/local/LocalPrivateStore.cpp)、[CID SQL桥](../../services/message/local/MySqlLegacyIdentity.cpp)、[发布器](../../services/message/local/LocalEventRelay.cpp)、[真实Message组合](../../services/message/local/NativeMessageRuntime.cpp)。

## 新的暖路径归因，不能与旧完整系统P99混用

R130真实Windows客户端：1024连接、32闭环工作者、128B文本，10240条新消息全部持久ACK/投递，512会话FIFO与历史通过。客户端ACK均17.383ms/P99 38.804ms。这是带诊断的闭环实验，不是固定700/s或容量结果，不能拿它与上面的Peer直接计算提速。

58个同MID的确定性共同样本中，Gateway排队均5.182ms、权限1.094ms、持久化RPC7.797ms；RPC内部服务5.598ms、native提交5.411ms。RPC外侧2.198ms含传输、入口前准入与客户端调度，不能标成纯网络。

native内部的不重叠均值：引擎同步写2.319ms、事件准入2.032ms、身份桥0.783ms、用户锁0.113ms，其余索引读取/编码/通知及小段工作。准入包含健康检查，其中mutex等待2.004ms，占native样本均值约37%。补充分段后未覆盖时间由2.175ms降至0.016ms。健康检查、准入、内部锁属于包含关系，不能重复相加；阶段P99也不能相加。

结果：[同MID分解](../../benchmark/results/peer-20261017/PERF126-admission-analysis-r1-r2/result.json)，[逐MID原始行](../../benchmark/results/peer-20261017/PERF126-admission-analysis-r1-r2/same-mid-rows.json)。共同样本仅58条，不能把其阶段尾值包装成全量尾分布。

独立关闭后核对：33448个M/I/K/H/A索引数量一致、R全映射一致，N/D/C/U逐值核对，全部confirmed，E/F/P/Q清零。A是留存事件档案，不能误判为待办。新只读broker审计将所有33448事件体与A档案逐字节比较，另核对21群事件；全量Redis、连续5次消费diff/inflight=0，原20服务与全部1982容器保持。见[后台完整核对](../../benchmark/results/peer-20261017/PERF129-exact-native-projection-r131-r1/result.json)。这不替代实际broker故障窗口实验。

## 为什么仍未优化到位

现有正式改善主要是SQL/RPC往返微调，保留了大部分原工作量；已知负结果没有得到稳定端到端收益。更大的本地持久化改动已落地为可选候选，但兼容接入期间检出了历史顺序、群事件topic与冷登记锁问题，已逐项修复/验证；尚未完成无诊断、等状态、固定负载的完整Peer复验。不能把候选组件成绩当成已经替换了完整系统。

后续顺序必须是：健康共锁候选正确性→同ELF OFF/ON/ON/OFF→全部后台精确收尾→无诊断SQL/native/Peer固定工作点→真实容量、长稳和故障恢复。每个候选保留源码、开关、失败轮、正确性和资源分母；不提前ACK，不停后台，不删除持久接收状态来换成绩。

## R132/R133：该健康共锁候选已经做完四轮有限消融

同10个ELF（Message R29），1024连接/32闭环工作者/128B，每轮2048消息。实际容器全部有效flag核对，仅Message的健康读取开关变化，其他flag、二进制和资源限制一致。OFF/ON/ON/OFF共8192条精确持久ACK、投递、512会话FIFO/历史及关闭后全量native核对通过。

|轮次|开关|ACK均值 ms|ACK P99 ms|投递P99 ms|共同MID数|健康检查均值 µs|
|---|---|---:|---:|---:|---:|---:|
|r1|OFF|21.9424|50.5427|57.4056|16|1714.625|
|r2b|ON|17.2091|35.9607|41.0152|17|0.765|
|r3|ON|14.9725|31.8975|37.0321|16|0.688|
|r4|OFF|17.5947|38.2408|41.7608|15|1234.000|

两方向ACK P99观察降低28.85%/16.59%，投递P99降低28.55%/11.32%。ON不再取该mutex，对应mutex子区间是未执行，不能把缺失采样写成测得的0µs。共同样本为不同轮的稀疏MID，不能逐条跨轮配对或把其阶段尾值推广到2048全部请求。

取舍：保留隔离候选，**默认OFF**，进入无诊断完整固定负载复验。每轮流量窗口约秒级、历史逐轮增长，没有恢复同一历史/缓存；首次OFF在正常重启与构建后，顺序和缓存效应尚未排除。没有整机稳态资源窗口或本轮Peer臂，这不是持续容量或已追平结论。[比较与后台闭环](../../benchmark/results/peer-20261017/PERF130-health-abba-comparison-r2/result.json)。

真实后台收尾升级为41640条私聊事件体与留存A档案逐字节一致，另21群事件准确；Redis全量未读计数一致，连续5个consumer diff/inflight为0，未重放/重置/补写。原20服务、全部2026容器、旧9库、binlog保持。[收尾结果](../../benchmark/results/peer-20261017/PERF129-exact-native-projection-r133-r1/result.json)。

另一次错误路径审计发现：原Abort先给错误字符串赋值，分配失败可能跳过新健康状态置零及通知。R30将健康置零提前，并使可选诊断分配失败仍通知/回调；新增bad_alloc故障注入，普通与ASan/UBSan发布器各123检查、应用各89及16次实际终止恢复通过。R29/R30源码和ELF分开封存，**以上四轮性能数字仍属于R29**；R30加固版本待正式负载验证。[R30资格](../../benchmark/results/peer-20261017/PERF105-application-qualification-r30/result.json)。

失败也保留：arm2在发流前磁盘门槛拒绝，0条发起，重试使用r2b；有限启动预留从96MiB增至256MiB，正式15%+650MiB门槛不变。约132MB临时占用尚未归因，随后独立owned Redis启动观察只出现53,248B峰值下降，**不支持将历史差异归因于Redis或网卡**。PERF-131保存原门槛记录、恢复和下一采样。R1复制的文字标签10240/33448不准确，实际数值字段、2048逐请求和35496存储核对一致，未使用文字标签作为分母。

已将仅处于ARCHIVED状态的8个旧系统日志文件备份至E、实际独立恢复并核对后移出Guest；活跃日志保留。状态判据来自[systemd日志格式说明](https://systemd.io/JOURNAL_FILE_FORMAT/#dirty-detection)，不是按文件名猜测。全部源码和受保护容器/快照/数据库/binlog均保留。
