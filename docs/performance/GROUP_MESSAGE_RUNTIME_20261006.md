# 群消息现有执行器隔离候选（2026-10-06）

状态：源码准备，未构建/运行/验收。前一轮路由批约60ms→4ms后100人ALL仍301–320ms；第二次partial单变量无稳定平均收益。真实限频ACK样本入队至开始99–103ms，Get+Confirm执行19–20ms。代码核验Group peer/ACK使用business_executor_，生产config只有4worker/64stripe/512pending；私聊早已有独立message_executor默认8worker且有界512pending/64stripe，线程总数已经存在。

strict defaultOFF TINYIMX_GROUP_DELIVERY_MESSAGE_RUNTIME_ENABLE=1，只把HandleGroupMessageDeliveryAck和HandleGatewayForwardGroupMessageRequest两个入口同时选择现有message_executor。消息执行器未安装时保留原control fallback；已安装而draining/stopped时拒绝并走原恢复，不切换排序域。两入口继续原GroupDeliveryOrderingKey(mid,authenticated UID)/MustRun/epoch/cancel/submissionreject/队列上限/剩余budget。Execute方法字节完全保持：peer lease授权/完整持久化Get/新鲜本地TCP/去重/Tracker-before-send，ACK durableGet/已登记seq evidence/Confirm UPDATE status<>3/不确定结果处理均不变。

不新增线程、不改worker数/公平调度/CPU affinity/池大小/主机应用/持久化/期限/租约/恢复。Gateway class layout及virtual不变，只重新编译GatewayServer.cpp；已封闭route/claim/completion/API等对象须精确SHA证明，不复用不兼容Main。定时器直接线程安全tracker重试、原replay专用执行器、coordinator已提前Stop、原message executor先于Gateway依赖销毁drain均保持。群send/auth/治理仍原控制入口，后续若有热点需单独定位。

原生计划：strict OFF/ON/非法01/0；固定两个现有1worker执行器，阻塞群peer后原ACK同identity FIFO，ON下profile控制功能独立前进；private与group在同message域的竞争明确验证。原boundedhotkey拒绝/BeginDrain/stopped拒绝无fallback/生命周期计数和原27 tracker全套保持。原14executor/recipientFIFO回归与封闭镜像检查后实际相同image仅runtime旗标ABBA；route/completion/claim/partial/recipient/defer/commitwake固定。需要每轮真实所有收件/SQL3及完整公开功能交叉、再万人混合私聊/列表/群确认共同验证，不把隔离原生检查等同容量。

全部结果/问题/代码Git/预映像保存；当前全功能目标仍OPEN，2万混合FAIL和5万/文件容量/离线故障/AI原配置和CPU推理待完成。


## 原生与封闭镜像已通过，真实ABBA待运行

源码04a95c6、助手41ad443。124新隔离/原27tracker四模式（strict OFF/1/01/0）+原executor14+recipient排序6共144PASS；阻塞群peer时ACK不越过同identity，ON控制profile独立前进，private与群共享message域的竞争保留；boundedhotkey、BeginDrain、已装停止执行器不回退、全部计数drain零pending均通过。GatewayServer重编译一次、其余archive成员逐字节一致；class layout/原Main、所有封闭输入保持，无运行ELF测试wrapper。当前tag codex-group-message-runtime-gateway-v1-20261006，image818bea357d99761903ff9902729474884d4ad6677a838e284b33267ad3099f72，ELF066acab345ec029e5876bd21a89fd8a9b9a1fa4324da59123eaf6a13ccace156；Message f9094e0f/baa2ec54不变。继承164协调器/128Resolver/720Redis/62MessageRPC与旧协议/应用的相同对象及输入已核验，未宣称本次重复运行这些组。Docker UID1000/network none/read-only ldd-r和缺配置退出检查PASS，19服务/配置/借用库不变。

只读定位链接process凭据又曾误假设在runtime-private；find后找到stage根目录，未修改任何数据。真实同image仅message-runtime0/1/1/0，partial/route/completion/claim/recipient/defer/commitwake固定1；528消息/23628真实收件确认及四完整公开链216操作148断言，原3秒截止/SQL观察上界保持，失败和精确restore完整保存。此控制是100连接规模微对照，不代表私聊混合容量或全部10k–50k功能验收，结果pending。


## 真实执行器单变量ABBA完成，整体目标仍FAIL

控制ccbfad9。仅群message-runtime0/1/1/0，partial/route/completion/claim/recipient/defer/commitwake固定1，同818bea35/066acab3 Gateway及f9094e0f/baa2ec54 Message。528消息/23628真实wire及SQL状态3、216全功能操作148断言、0观察重复；原3秒ACK/ALL截止和SQL确认观察上界保持。全部原始结果在group-message-runtime-control-20261006和cross-feature-gmr1featA1/B1/B2/A2，每格30测量+3warm，不估总体P99/容量。

| case | size | ACK mean ms | ALL mean ms | ALL max ms | 原100ms样本门槛 |
|---|---:|---:|---:|---:|---|
| A1 off | 2 | 22.127 | 36.780 | 46.898 | PASS |
| A1 off | 16 | 24.703 | 53.200 | 81.711 | PASS |
| A1 off | 65 | 33.677 | 112.124 | 201.269 | FAIL |
| A1 off | 100 | 52.807 | 252.403 | 351.126 | FAIL |
| B1 on | 2 | 20.493 | 33.428 | 44.134 | PASS |
| B1 on | 16 | 23.676 | 50.763 | 77.844 | PASS |
| B1 on | 65 | 33.505 | 113.247 | 177.415 | FAIL |
| B1 on | 100 | 45.223 | 221.367 | 282.022 | FAIL |
| B2 on | 2 | 19.661 | 32.156 | 42.827 | PASS |
| B2 on | 16 | 25.793 | 53.378 | 74.163 | PASS |
| B2 on | 65 | 35.588 | 123.273 | 162.504 | FAIL |
| B2 on | 100 | 48.412 | 240.540 | 361.091 | FAIL |
| A2 off | 2 | 19.703 | 32.302 | 40.346 | PASS |
| A2 off | 16 | 22.747 | 50.587 | 63.242 | PASS |
| A2 off | 65 | 31.327 | 105.525 | 155.038 | FAIL |
| A2 off | 100 | 47.160 | 278.067 | 357.280 | FAIL |

100人ON221.367/240.540ms，对OFF252.403/278.067ms，保守均值减少4.700%。65人ON113.247/123.273ms，OFF112.124/105.525ms，没有稳定收益。B2 100人361.091ms也超OFF两轮max351.126/357.280，所有失败保留。控制隔离原生正确，但私聊与群共用消息执行器的容量尚未验证，不能称全功能性能突破。

| case | kind | biased samples | queue age ms | durable Get RPC ms | execution total ms | Confirm RPC ms |
|---|---|---:|---:|---:|---:|---:|
| A1 | peer | 122 | 2.832 | 4.132 | 4.267 | not measured |
| A1 | ACK | 154 | 92.291 | 5.616 | 16.707 | 10.992 |
| B1 | peer | 136 | 6.328 | 7.632 | 7.815 | not measured |
| B1 | ACK | 56 | 30.335 | 20.794 | 46.101 | 25.221 |
| B2 | peer | 148 | 8.247 | 8.271 | 8.389 | not measured |
| B2 | ACK | 58 | 45.383 | 20.643 | 45.280 | 24.558 |
| A2 | peer | 124 | 5.751 | 4.345 | 4.479 | not measured |
| A2 | ACK | 173 | 99.751 | 6.562 | 18.138 | 11.485 |

ACK排队ON30–45ms，对OFF92–100ms减少，但ON Get约20.6–20.8ms/Confirm24.6–25.2ms，OFF Get5.6–6.6/Confirm11.0–11.5ms；peer Get也4.1–4.3→7.6–8.3ms。8worker并发把排队转移到下游，共享budget的抽样选择明显改变，不能从这些不同样本估P99或直接认定pool/fsync唯一原因。64completion均值OFF20.120/24.249→ON29.294/33.360ms也变慢，claim约24.6–25.3/dispatch3.36–3.94ms。线程CPU远小于RPC墙钟。下一先用当前Message已编译TINYIMX_MYSQL_POOL_TRACE（strings核验）和只读Performance Schema窗口delta，将池slot/PING、语句成本与锁/文件等待区分，避免盲增线程/池或删Get。

只读group-message-runtime-sql-readonly-20261006捕获累计历史统计：durableGet225003次平均0.674ms，单条Confirm149153次平均6.774ms，batchCompletion1851次平均19.429ms；当前digest被截断，累计值混合所有历史运行，不能用它们直接代替本轮RPC时延或claim因果。消费者/文件instruments/全局row_lock/fsync/rawproc完整保存；没有SET/清零/新负载。已证明候选Message原池阶段诊断可用，无需为定位重新编译。

最终原Gateway5c2645/e687+Messagebe8/2548及完整Env/HostConfig/Health/Mounts恢复，其他16精确实例/配置/持久化1/1/1/0/0/宿主应用保持，自己的actors关闭，三次candidateMessage停止exit0。最近实例以group-message-runtime-control-20261006/restore-summary.json为准。所有源码/失败/原生/实际对照/恢复保留，候选尚未选入容量运行。

全功能只读审查另确认AI是compose ai profile的一次性CLI，当前19常驻没有AI容器；原config仍host.docker.internal:11434/qwen3:8b，实际Ubuntu localhost11434已有qwen2.5:7b。现有socat及用户systemd可用，下一需审计可恢复的项目网络桥接和精确模型配置后做AI/MCP全链，不在其他性能测量时推理。MCP会话/history源码maximum50已修正，不能重复修改旧历史报告的100问题。原生产MCP static principal仍需核验，不擅自把身份替换为测试用户。

准备/审查错误均保留：只读尝试匹配不存在运行AI容器时向docker inspect传空列表；一个复杂远程SQL shell引号在PowerShell解析前失败，后使用独立只读ScriptPath完成；数次源码通配Literal/旧路径由rg或find纠正。没有guest数据库/配置/代码影响，避免下一轮重复这种路径/工具组合错误。
