# 全部功能优化与验收清单（2026-10-06）

当前状态：会话未读批量优化已在当前两个 Gateway 运行。实际 Redis API 248 个检查、400 个真实固定 50 项列表响应、10k 混合负载四轮、全功能操作链及真实 Ollama/MCP 均有保存结果。**全部功能在 10k/20k/30k/50k 达到极致性能的总目标尚未验收。**

同 Gateway ELF，仅切换会话未读开关的 ABBA 中，10k 长连接+100 条私聊/秒+单个列表用户20次50项查询/秒，会话页P99从232.961–246.504ms降为38.092–45.531ms；私聊持久化ACK P99直方图上界从86.1–93.9ms降为59.4–70.5ms。这是指定负载结果，10k长连接不等于10k请求/秒，不能写成50k全业务容量。

完整原因、对照数字、失败记录、Git迭代、运行映像和恢复路径见 [会话未读优化结果与全功能问题复盘](CONVERSATION_UNREAD_BATCH_RESULT_20261006.md)。

## 功能验收口径

按源码真实33类Gateway入口逐项登记。28类公开请求来自实际协议操作链；私聊/群聊接收ACK和Pong实际发送核对。内部转发经跨网关业务流间接覆盖，独立内部认证负例/故障恢复不能据此标PASS。公开链每轮4名新自有用户49项操作，正确性不能代替每类功能万人业务频率性能。

| Gateway 类型 | 本轮功能正确性 | 混合交互证据 | 20k/30k/50k 每类功能性能 |
|---|---|---|---|
| 1001 / kLoginRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2001 / kChatMessage | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2020 / kChatDeliveryAck | 真实接收端 ACK 验证 | 私聊 24000 条/群聊低样本 | NOT_RUN |
| 2052 / kGroupMessageDeliveryAck | 真实接收端 ACK 验证 | 私聊 24000 条/群聊低样本 | NOT_RUN |
| 3001 / kGatewayForwardChatRequest | 跨网关业务流间接覆盖，独立入口未验收 | 专门内部认证/故障测试 NOT_RUN | NOT_RUN |
| 3003 / kGatewayForwardGroupMessageRequest | 跨网关业务流间接覆盖，独立入口未验收 | 专门内部认证/故障测试 NOT_RUN | NOT_RUN |
| 2003 / kReadRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2005 / kHistoryRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2007 / kConversationListRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2021 / kUserProfileRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2009 / kFriendListRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2011 / kFriendRequestCreateRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2013 / kFriendRequestListRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2015 / kFriendRequestAcceptRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2017 / kFriendRequestRejectRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2023 / kCreateGroupRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2025 / kGetGroupRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2027 / kUpdateGroupRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2029 / kDisbandGroupRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2031 / kJoinGroupRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2033 / kLeaveGroupRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2035 / kInviteGroupMemberRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2037 / kKickGroupMemberRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2039 / kSetGroupMemberRoleRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2041 / kSetGroupMemberMuteRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2043 / kTransferGroupOwnershipRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2045 / kListGroupMembersRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2047 / kListMyGroupsRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2049 / kGroupMessageSendRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2053 / kBeginFileUploadRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2055 / kGetFileUploadSessionRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 2057 / kCancelFileUploadRequest | 真实公开操作链 PASS（低样本） | 10k 四轮操作链 PASS | NOT_RUN |
| 9001 / kHeartbeat | 真实 Pong/完整 drain 验证 | 10k 每轮全部发出心跳得到响应 | NOT_RUN |

## Gateway 外部能力与未完成场景

| 能力/场景 | 已有证据 | 尚未完成 |
|---|---|---|
| 文件流上传/续传/下载/完整性 | 多轮真实1.8MiB文件字节/SHA、跨所有者拒绝、幂等取消 | 并发大文件吞吐/P99/故障续传/万人容量 |
| MCP | 两轮真实8域工具、鉴权/actor注入负例 | 高并发工具容量/故障恢复 |
| AI/Ollama/tool loop | 冷调用FAIL保留；就绪后2次真实工具调用PASS | 秒级推理仍慢，原生产profile仍错误，并发/P99未验收 |
| Outbox/MQ/未读投影 | 24000私聊实际接收及原SQL/ACK/MID/收发身份逐条对账，未读原值一致 | 离线重连/故障重放/积压恢复/乱序交互 |
| 登录/会话/心跳 | 10k四轮全部登录、0断连、全部心跳drain；此前登录阶段诊断 | 20k–50k同版本混合、账号争用/重连 |
| TLS/热点群/慢消费者/过载恢复/soak | 本轮NOT_RUN | 分别测试，不从明文私聊/49操作外推 |
| 好友/群列表SQL | 已有JOIN/有界游标查询的源码证据 | 不套用会话未读逐项Redis根因；热点和慢写操作先定位 |

## 本轮发现与继续迭代

已解决会话页50次Redis租借/健康PING/GET，100个网络命令改成一次原健康获取和有界只读Lua。服务器GET仍50个，减少租借/往返，保留int64/wrongtype/顺序/错误语义。第一次隔离构建错认静态库成员，原失败保留，新的attempt2按真实CacheService链接库修复，原库未覆盖。

AI原profile桥接地址不可达且模型不存在；新私有测试配置连接已有Ubuntu loopback/qwen2.5:7b。冷加载被30秒请求取消，独立startup readiness85.070秒；就绪Agent43.313/19.928秒，仍未达性能目标。原生产profile不宣称已修复。

单次file-begin150.503ms/group-create134.176ms保留，低样本不能当P99或直接归因数据库。下一步阶段定位后再改代码。

接下来当前已接受版本20k长连接+135私聊/s+20列表/s+同公开交互链，保留原100/s总登录爬坡、原期限/指标。失败先分析，不跳过失败直接冲50k。

所有变更有预映像/详细审计、显式Git添加。原私有配置、宿主游戏/其他应用、MySQL持久化1/1/1/0/0、密码验证、Redis健康检查保留。数据与失败保留，无删库/业务文件删除/缓存清空/强制模型卸载。

## 后续20k实测已保存

20k长连接+135私聊/s+20会话页/s+公开交互链已完成。8100私聊/1200完整页/49操作36断言正确，231524发出心跳全部响应；延迟FAIL，原ACK P99上界201.0ms，会话页P99 155.029ms。未放宽期限/漏掉失败。相同MID阶段分析及准确的下一步见 [20k失败复盘](MIXED20K_BOTTLENECK_AND_NEXT_20261006.md)。以上“下一点20k”是先前准备时记录，当前状态以本段和新复盘为准。


## 20k Redis阶段诊断已经完成

在全部公开操作链保持的同一20k负载中，诊断候选8100私聊、1200完整会话页、49操作/36断言正确，但ACK P99上界149.4ms、列表P99 107.684769ms仍FAIL。该轮仅加低频诊断，不是新性能修复；已恢复接受的a2bb网关。935条租借抽样多数8个slot全空闲，等待主要在健康PING，不能盲扩池；10个同MID慢样本仓库commit仍突出。精确数值/偏置限制/全部恢复身份见 [Redis阶段实际结论](REDIS_ACQUIRE_DIAGNOSTIC_20261006.md)。

表中最后一列表示每类功能20k/30k/50k均完成的容量性能验收：目前没有这种验收。已单独测量20k私聊、会话页和混合49操作，并保留FAIL；群聊/文件/好友等低样本正确性不能代替它们各自高频/并发/P99/故障恢复。下一候选先控制等待机制，再逐功能验证，全部功能极致目标继续OPEN。


## 群生命周期与真正的交付延迟

五RPC服务共用信号初始化顺序与注册就绪缺口已修正源码，97原生就绪检查PASS；仅Group候选实际四轮800查询/216交互操作/148断言PASS，并验证四次优雅退出。现有运行最终恢复原Group，其他18不变。群JOIN串行延迟有重叠，容量未接受。复盘见RPC_READINESS_LIFECYCLE_20261006.md。
相同MID端到端群收件事件发现发送ACK约20–26ms，全部收件却149–695ms；原每秒扇出恢复轮询没有提交唤醒。下一候选补有界事件通知，保留durable claim/租约/恢复原语，见GROUP_FANOUT_COMMIT_WAKE_20261006.md。当前是4消息/12收件样本，不能当P99，也不从私聊发送ACK推断群实际到达合格。全部功能目标仍OPEN。

## 群提交唤醒的实际对照已完成

18原生检查及镜像PASS。同Gateway ELF、OFF/ON/ON/OFF四轮2/s实际跨Gateway群收件共280条、240测量：关闭实际收件mean298.755/329.222ms，开启27.825/24.409ms；每轮60样本P99=最大值，关闭601.093/780.649ms，开启40.625/41.536ms，保守减少93.090%。严格核对全部消息、收件内容、身份、SQL状态3和原接收ACK；无重复wire/超时/跳过。
216公开操作148断言的4轮完整功能链正确，但不以低样本链作为每功能万人P99。原运行最终恢复a2bb/原全Env，其他17实例及持久化保持。完整原因、Git、失败修正和恢复见GROUP_FANOUT_COMMIT_WAKE_20261006.md。当前下一步是将群真实收件加入10k混合负载，再验证热点群/串行逐收件完成RPC；20k私聊及会话延迟FAIL仍保留，AI/文件容量仍OPEN，全部功能极致目标未完成。

## 群实际收件进入万人混合后的结果

相同5c2645 Gateway ELF、唤醒OFF/ON/ON/OFF，10000连接+private100/s+完整50页20/s+真实群收件2/s+54操作交互链均在原60秒窗口。24000私聊/4800完整页/280群消息逐条正确，216操作152断言（含窗口校验）正确。ON两轮private ACK P99上界57.5/50.9ms、列表P99 38.293/34.199ms、群实际收件样本P99 84.044/79.622ms，通过原100ms门槛；OFF群交付1036.301/1021.919ms性能FAIL完整保留。群只有每case60测量，两人群不能代表大群。
此候选满足限定万人混合准入，准备选择继续运行；实际选用以accepted-group-fanout-wake-20261006/summary.json为准。20k–50k各类功能/热点群/高并发文件/AI原生产入口和推理/故障恢复仍OPEN，100ms仅门槛，继续实测减少必要往返与串行等待。完整数据与Git见GROUP_FANOUT_COMMIT_WAKE_20261006.md。

## 当前已选用运行版本与下一规模分析

accepted-group-fanout-wake-20261006实际receipt已PASS：当前两个Gateway image5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405，ELF e68731562d83b3b5c1923d80ed7ad4459d2f5cbc9b8a224e32014626fac04cfc，commitwake=1、conversation-unread=1、online-maintenance-batch=1；A CID cb5446335cfde72634157b564789b843d2335e90ec4d9577427c8ab7e9c80a80，B ad3626c181645b00210a43ee904e1276f02037c8b1e166bd6d3cfe3add73eec9。19健康/其他17实例不变/完整配置和原持久化保持。部署后的3资料、完整50页、1条跨Gateway群消息实际收件及SQL状态3/Pong drain正确。完整a2bb回滚override SHA e204cf1092a3421c70a36f9bbb4ddc5cff8de5d211625841215dcc6b91fea2c7继续保留。

下一源助手只创建4个新自有2/16/65/100人群，固定100个自有连接分布两网关，分别13条消息/3warm10测量逐收件和SQL确认，原3秒ACK/交付期限保持，保留全部Group/成员/记录以便下一轮同夹具迭代。10个消息样本不估总体P99/容量。源码实际公开群上限500；内部recipient snapshot guard5000不能当5000人群支持。观察租约逐行UPDATE、逐收件完成RPC，以及peer receive/ACK按同MID串行的影响，准确分解后再改代码。此次仅准备规模分析，尚未标结果PASS，全部功能极致目标继续OPEN。

## 群规模实际结果与有界投递方向

2/16/65/100人群52消息/2327真实wire+SQL确认正确、0重复，全收件mean32.319/133.498/482.334/767.800ms，max38.675/160.643/534.799/863.790ms。16/65/100性能FAIL保留，10样本不能估P99或万人容量。当前默认OFF候选只将已提交同批有界dispatch置于原completionRPC之前，保留租约/重试/授权/持久化；低频阶段计时配合同Group/ELF开关对照验证瓶颈。见GROUP_FANOUT_PIPELINE_20261006.md。全部功能高频交叉20k–50k、文件/离线恢复/AI性能仍OPEN。

## 群完成RPC迭代真实结果

36native和同ELF4case208群消息/9308收件/216操作148断言正确。defer ON把65人mean481–508ms降至244–273ms、100人774–788ms降至536–559ms，仍FAIL保留。原accepted5c2645全Env已恢复，未把大群失败标成极致验收。下一strictOFF recipient排序+低频dispatch_age诊断，保持同收件重复顺序/鉴权/lease/SQL确认和原executor；准确阶段与Git见GROUP_FANOUT_PIPELINE_20261006.md。全部功能极致验收OPEN。

## recipient排序真实结果与全功能边界

56native/原executor契约修正及historicalfair exact1FAIL均保存；同ELF recipient OFF/ON/ON/OFF且defer1固定，208群消息/9308实际recipientconfirmations/216操作148断言正确。16人两ONmean67.093/66.224ms，max78.005/73.387ms，65人mean195.464/200.280、100人443.023/424.176ms仍FAIL。原5c2645完整Env最终恢复；新候选尚未运行万人混合/未接受。

低频peerqueueage OFF33.841/37.950→ON3.056/5.984ms，ACK133.636/146.212→47.497/42.258ms，阶段抽样人口/温身不同不估P99；更并发时RPC变慢同样记录。继续减少groupclaim逐行UPDATE和跨batch completion必要等待，保留durable/auth/确认；文件/AI/其他所有feature容量和交叉仍OPEN。完整实际镜像/ELF/CIDs/失败/Git见GROUP_FANOUT_PIPELINE_20261006.md。

## 群领取SQL实际结果与未满批问题

207项真实隔离SQL验证PASS；同MessageELF领取batch OFF/ON/ON/OFF、recipient/defer1固定，208群消息/9308实际收件及SQL确认、216操作148断言正确。64条领取阶段约93–94ms→33–36ms，但两ON65人全收件mean237.915/215.675ms，100人453.840/484.238ms仍FAIL；16人B1max113.177ms也FAIL。原网关5c2645+Messagebe8及完整Env恢复，其他16/配置/持久化/主机应用保持，下一万人混合尚未进行。完整表、native、precise恢复及Git见GROUP_DELIVERY_CLAIM_BATCH_20261006.md。

保留A2 MID1304全部慢请求：49人约202ms到达，余15人约一秒后才开始，ALL1117.585ms，attempts均1。源码将不足64条领取当作可等待1000ms，SKIP LOCKED并发下这不成立；下一defaultOFF有界部分批drain先原生重现验证，再对照，不能把该修正当作完成RPC的全面解法。全功能20k–50k高频/文件传输并发/离线故障/AI原profile及推理仍OPEN；100ms只是门槛，所有FAIL及原日志保留。


## 部分批drain真实对照与下一主瓶颈

92原生检查PASS；真实OFF/ON/ON/OFF共528群消息、23628实际收件SQL确认、216全链操作148断言正确。100人全收件mean530.578/497.820/486.567/489.157ms，65人221.350/208.863/212.008/216.701ms，均FAIL。16人B1max115.674msFAIL也保留。30消息不估P99；本轮两模式没有复现此前877ms部分批空档，机制修正不能宣称实际因果增益。64批完成RPC均值308–319ms约占整批75%，继续优先减少该明确串行阶段；领取30–33/派发63–71ms不能误归因一个原因。原5c2645+be8全Env恢复、其他16和配置持久化/宿主应用保持，新候选尚未选入万人混合。全部功能验收仍OPEN，文件幂等begin重复完整SQL读取也进入下一独立候选。见GROUP_FANOUT_PARTIAL_DRAIN_20261006.md。


## 文件幂等优化与注册窗口真实失败

已保存93e2e6a9文件snapshot候选；150真实SQL检查和原应用层PASS，仓库层重复读取/健康PING2→1，pool1/4均值约3.4–3.7ms→1.5ms。真实入口首轮保护Binds顺序误报、第二轮OFF154重复成功后第155请求在服务发现阶段失败：旧注册session32.568过期，新File32.686才注册完成，32.600失败未进入RPC/SQL。所有失败/成功/恢复保留，ON和全部4功能链没有运行，无入口性能结论。原File/其他18/配置/持久化恢复。当前v2组合原97PASS File注册就绪与同一snapshot机制，namedready/信号修正固定OFF/ON后再实际对照，不能把这一稳定性修正算作snapshot性能收益。群64批completion308–319ms仍主瓶颈，群大规模/其他功能交叉容量全部OPEN。见FILE_BEGIN_UPLOAD_SNAPSHOT_20261006.md。


## 文件实际查询优化对照已经通过

保持registeredready固定同ELF，仅snapshotOFF/ON/ON/OFF：1920真实入口测量+480四并发重试及2636文件检查、216完整操作148全链断言正确，真实传输每轮字节一致。已有begin均值关闭6.845720/6.953903ms，开启5.220292/5.019929ms，保守减少23.744%；480样本P99关闭8.904546/9.194251ms，开启7.396021/6.824712ms。只有4closedloopactor，不以该结果作为万人容量P99；4并发为pump观测上界。三个候选退出0/普通线程mask/namedready/真实注册均验证；原File全部Env/Health/Mounts恢复，其他18/私有配置/持久化/宿主应用保持。早先两轮失败并保留，不接受遗漏或替换失败。尚未选入长期运行；文件高并发真实传输/全部功能20k–50k和AI仍OPEN。下一独立群完成批SQL API针对实测308–319ms阶段，先真实隔离SQL验证再RPC接线。见FILE_BEGIN_UPLOAD_SNAPSHOT_20261006.md。


## 群批完成真实突破与全部功能继续开放（当前结果）

128协调器、62真实MySQL/gRPC检查及旧协议/应用测试通过。实际同Gateway+Message ELF，仅批完成OFF/ON/ON/OFF变化，共528群消息/23628真实收件及SQL确认、216完整功能交互操作/148断言正确，0观察到重复。64批物理完成RPC64→1，阶段均值312–329ms→34–45ms；100人全收件mean关闭564.264/484.475ms，开启352.113/350.801ms，保守改善27.321%。65人开启217.133/204.933ms，仍FAIL，且与关闭A2有重叠；16人关闭max108.522/101.073ms失败均保留。30样本不估总体P99，100实连接不等同万人容量。完整实际请求/失败/恢复/Git见GROUP_DELIVERY_COMPLETION_BATCH_20261006.md。

剩余领取约32–34ms、64次逐人派发约64–67ms、对端durable read约4–6ms及排队仍实测存在。下一有界路由批读取先保留uint64/每key错误/TTL/真实TCP与对端授权、持久化校验，再隔离验证和实际对照；不能把一项批处理当作所有功能已经达标。当前最终恢复原5c2645网关+be8Message完整配置，其他16不变/持久化1/1/1/0/0/宿主应用保持；最近恢复receipt是group-completion-batch-control-20261006-attempt2/restore-summary.json，以其CID为准，前文CID仅历史。

全部功能目标继续OPEN：1万已通过的是既定私聊/会话页/两人群混合门槛；2万混合延迟FAIL尚待突破；热点大群仍FAIL；资料/好友/群管理/分页/权限交叉仅低样本功能正确；文件snapshot已有限入口对照通过，文件高并发真实传输容量未验收；离线补发、断连恢复/故障/长稳态容量未验收；AI原生产服务地址/模型配置与CPU推理几十秒问题未解决，不能计为100ms达标。后续每次迭代必须保留功能链和失败原值，不扩大结论至未测功能或5万用户。


## 路由批读取真实结果与下一准确瓶颈（当前）

真实Redis720/Resolver128/协调器164、继承62Message RPC和旧协议/应用通过。Route同image OFF/ON/ON/OFF，528消息/23628实际收件及SQL状态3、216全功能交互操作148断言正确、0观察重复。64路由派发约60–62ms→4.22–4.32ms；65人ALL均值201–202→125–128ms，16人75→60ms。100人仍301–320ms，保守相对OFF改善仅1.255%，未达标；A2 100人1203.057ms及16人142.915msFAIL保留，30消息不估P99或容量。完整复盘见GROUP_ROUTE_READ_BATCH_20261006.md。

MID2895前48人189.004ms收到，网关B partial48批后，网关A直到1060.946ms才开始剩51条领取，ALL1203.057ms；902.123ms空档与不足64条进入1000ms恢复等待相符。低频未测完整锁时间线，下一同候选保持route1/completion1/claim1，只切有界partial drain验证；其原生调度回归已在当前镜像重新通过。另一瓶颈原4工作线程内ACK排队抽样约99–103ms，每人durable Get+Confirm执行约19–20ms；不删授权/序号证据，不盲扩池或线程，不降持久化。既有InsertGroupMessageDeliveries早已TX内bulk256，避免错误方向。两问题分别分析、单变量验因。

当前已完整恢复原Gateway5c2645+Messagebe8/fullEnv及其他16/配置/持久化/宿主应用；最近实例以group-route-batch-control-20261006/restore-summary.json为准。2万混合FAIL、5万全部功能/热点大群、各功能高频交叉、文件并发真实传输、离线/故障/长稳态、AI原生产配置和CPU推理仍未验收，不能把低样本完整功能链或一项成功泛化为全功能极致。


## 第二次部分批对照未显示性能收益，保留负面结果

route/completion/claim固定1，仅partial0/1/1/0，528消息/23628真实收件及SQL状态3、216全链操作/148断言、0观察重复正确。100人ON334.490/321.398ms，OFF327.232/315.551；65人ON139.113/136.124ms，OFF130.720/131.576，均FAIL，未显示稳定平均收益。两模式均未重现902ms空档，不以长尾缺失宣称修复实测因果。已恢复原运行，最新restore为group-route-partial-drain-control-20261006/restore-summary.json。见GROUP_FANOUT_PARTIAL_DRAIN_20261006.md。

新源码审查确认群peer和ACK都仍提交business_executor_4worker；现有私聊使用message_executor_原默认8worker，并且已有优雅drain。原控制功能与群慢RPC因此竞争；下一只移动两入口至现有消息执行器并同identity排序，保持无新增线程、鉴权/SQL/持久化/重试/全功能交互及私聊共用压力验证。该方向尚未实现/测量，不宣称达到全部功能目标。20k混合FAIL、50k全功能、文件容量/故障/AI仍OPEN。


## 群执行器已修正接线并完成真实对照（当前结果）

严格默认OFF候选04a95c6复用既有message_executor，只有群peer/ACK两个提交入口变化，其余执行/授权/序号/SQL/类layout逐字节保持，无新增线程。144原生隔离/FIFO/原tracker/拒绝/关闭检查通过。真实同镜像0/1/1/0，528消息/23628真实收件与SQL状态3、216完整交互操作148断言、0观察重复正确。100人ON221.367/240.540ms，OFF252.403/278.067，保守均值改善4.700%；65人ON113.247/123.273ms没有稳定收益，65/100全部FAIL保留，30样本不估P99。原运行/全配置/其他16/持久化/宿主应用已恢复，以group-message-runtime-control-20261006/restore-summary.json为准。完整复盘GROUP_MESSAGE_RUNTIME_20261006.md。

ACK抽样排队92–100→30–45ms，但原durableGet/Confirm RPC分别约6/11→21/25ms，说明并发成本转移下游；不能继续盲扩线程或改低持久化。下一当前Message现成池阶段诊断+Performance Schema窗口delta，准确定位slot/PING/SQL/锁/提交。只读累计SQL平均Get0.674/Confirm6.774ms是历史提示，不代替本次阶段证明。私聊与群共享message执行器后的10k混合、2万FAIL复测，以及全部50k/各功能高频/文件并发/离线故障/AI目标仍OPEN。


## 群收件确认提交成本已由固定窗口验证（当前）

1bcda00只开现有池诊断+只读Performance Schema前后增量，两群/两模式共132消息10758真实收件确认、108全功能操作74断言正确。100人每33消息仍3267次单条确认SQL，4worker均值6.062ms/8worker11.160ms；durable SELECT仅0.637/0.946ms，不能把整个Get RPC墙钟归给SQL本身。更并发100人池0空闲8/102偏置样本，slot均值1.127ms，存在但不足以解释全部延迟；锁等待增量1478→3129ms，同时redo/binlog事件等待实际存在，不能把计数直接等同每条fsync或认定唯一瓶颈。完整方法、窗口原值、采样限制见GROUP_MESSAGE_RUNTIME_20261006.md。

下一先优化有界收件确认合并提交，保留每请求原0/1 affected_rows、缺失/重复/状态/序号/鉴权/持久化/不确定提交语义，先真实独立SQL和并发失败回归再入口对照。全部功能范围不缩减，低频正确不算容量；20k混合FAIL、50k所有功能/文件并发/离线故障/AI仍OPEN。已完整恢复原运行，当前实例receipt为group-runtime-sql-diagnostic-20261006/restore-summary.json。


## 2026-10-06最终现状审查：实际线程与测量方法纠正

实际两个Gateway环境TINYIMX_MESSAGE_WORKER_THREADS=16，每个消息执行器16worker；business4worker。前文8是代码默认值，错误地用于运行解释。runtime及SQL诊断实验同映像/完整配置固定，不影响单变量对照，但正确并发解释为4→16。原记录与数字保留，不按错误线程解释重复扩池或加线程。

最新确认合并387验证PASS、实际528消息/23628逐人wire与SQL状态3、216操作/148断言。确认UPDATE约减少88%，100人ALL两ON247.590/223.360ms、OFF261.200/241.108ms；最慢ON对最快OFF没有稳定均值收益，65/100仍FAIL，未部署。原运行完整恢复。完整结果见[当前性能与竞争力评估](CURRENT_PERFORMANCE_ASSESSMENT_20261006.md)和GROUP_CONFIRM_COALESCE_20261006.md文末。

群微测试send后同步重写累计证据，再读响应；同Guest离线重放100人后段约1.62MB写入平均47.310–61.132ms，编码CPU占大部分。99条合成接收记录逐条写平均11.319ms。不能从历史数字直接扣除或改FAIL，必须先移出测量关键路径并校验新旧观察，再定位全链。当前1万指定混合已通过，不等于每个功能高频/5万人容量，2万失败、文件高并发、AI原profile/推理、多端/故障/长期稳态未验收。


## 真实采集单变量ABBA完成：纠正方法，不冒充服务端突破

c4fc8c7，group-deferred-evidence-control-20261006；固定Gateway818bea35/Message645e9cc完整Env，Message coalescing0和其他candidate flags固定1，只切client同步/延后/延后/同步。25原生完整性检查PASS，528消息/23628实际wire与SQL状态3、216完整链操作148断言、0观察重复正确，原3秒ACK/ALL、SQL8秒观察保持。原5c2645/be8及全配置、其他16/持久化/宿主应用已恢复。

|case|2人ALL mean/max ms|16人|65人|100人|
|---|---:|---:|---:|---:|
|A1 off|32.423/37.960|50.392/69.773|102.077/132.147|215.251/284.230|
|B1 on|32.582/40.861|48.481/67.230|108.288/139.091|229.616/313.774|
|B2 on|32.706/41.499|48.793/65.204|106.682/158.175|218.368/282.147|
|A2 off|32.657/38.583|49.137/63.793|104.280/127.556|225.963/322.436|

延后ON的100人ACK均值40.798/37.438ms相对OFF47.214/43.437ms较低，但全收件均值229.616/218.368ms相对OFF215.251/225.963没有稳定收益；65人ON108.288/106.682ms，仍FAIL。16人仅保守0.699%均值差，不称实质突破。所有慢/失败样本保留，不能扣历史时间或改旧FAIL。

两ON分别7279/7276条captured记录与persisted一致（精确计数以receipt为准），峰值pending不超过299条、保守reservation约551KB，未触及8192/8MiB限制；时间戳/内容、chunk SHA与计数完整。保留延后采集是改善测量方法，不标记为产品性能收益。低频<=8/s的100人peer样本执行约10–11ms、CPU约0.45ms、Get约10ms，任务结束到用户空间观察约2.4–2.5ms；主要晚到还包括提交前批次与执行器等待，不能只算客户端I/O。

100人已捕获64人批次领取23–25ms、派发约4ms、同步完成约29–31ms，再领取下一批。源码RunOneIteration的完成RPC在下一轮claim之前，100人99收件跨批；是明确待验证屏障，不能当唯一根因。下一只测batch64/128/128/64，客户端延后1和coalescing0固定；现有claim/route/completion max256，所以128在既有边界内，不增线程/扩池/放松期限。完整数值/关联rawSHA见benchmark/local_capacity/results/group_deferred_evidence_20261006.json。


## 实际64/128 ABBA结果与结论

0d8f7752，group-batch128-control-20261006；固定deferred1/coalescing0，只改batch64/128/128/64。528消息/23628实际收件并SQL状态3、216操作148断言全部正确，0观察重复；25采集完整性检查同源继承。原配置及其他16/私有配置/持久化/宿主应用完整恢复，恢复receipt已保存。

|轮次|批次|2人ALL mean/max ms|16人|65人|100人|
|---|---:|---:|---:|---:|---:|
|A1|64|32.934/46.894|49.107/64.198|106.957/147.590|215.392/266.346|
|B1|128|32.695/39.043|49.646/66.376|113.294/164.056|151.856/216.746|
|B2|128|33.706/44.466|50.024/77.349|108.874/157.210|146.508/181.814|
|A2|64|32.317/45.349|50.092/67.485|111.130/135.878|213.724/294.741|

100人最慢ON均值对最快OFF改善28.9479%，有稳定的单项微测试平均收益；但max仍181.814–216.746ms，65人仍FAIL。2/16/65人保守mean差为-4.2987/-1.8677/-5.9256%，不宣传全局收益；30样本不估P99，不是10k50k容量。阶段捕获99人批次证明既有128确实覆盖99收件的一批（也有偏置部分批次记录），65人64收件原本就一批，所以无相应屏障收益；不同阶段均值不能直接相加。批次屏障是被验证的一项原因，不是所有延迟唯一根因。四轮采集captured==persisted、无失败，峰值299条/551020预算，原raw完整。数字及关联阶段见benchmark/local_capacity/results/group_batch128_20261006.json。

128下peer偏置queue约2.8–3.1ms，ACK queue84–88ms、Get17–20ms、Confirm26ms。下一只验证既有coalescer0/1/1/0在128固定条件下是否缓解确认突发，前64负结果不抹去。尚未部署128或更改默认64；mixed公平/过载/故障/长稳态/全功能5万仍OPEN。


## 批次128固定后的确认合并0/1/1/0实测结果

399822b，group-confirm128-control-20261006；同818Gateway/645Message，全其他Env不变，deferred1、batch128固定。528消息、23628逐人wire及SQL状态3、216功能操作148断言、0观察重复全部正确。每size/case30 measured不是P99，原3秒ACK/ALL及SQL8秒观察不变。387 native同SHA继承（49caller/276SQL/62RPC），未重复运行声明；所有失败/慢/不确定结果保留。结束原5c2645/be8与全Env/Health/HostConfig/Mounts、其他16/私有配置/宿主应用/持久化1/1/1/0/0已完整恢复，未部署新参数或修改默认。

|轮次|确认合并|2人ALL mean/max ms|16人|65人|100人|
|---|---:|---:|---:|---:|---:|
|A1|0|33.094/38.563|50.046/69.121|110.637/176.689|148.167/210.282|
|B1|1|33.392/42.135|48.786/59.903|98.352/168.869|135.327/196.602|
|B2|1|33.110/46.882|50.041/65.838|99.104/140.311|134.060/171.659|
|A2|0|32.624/35.840|53.083/74.649|107.343/152.120|149.377/183.786|

65/100最慢ON mean对最快OFF保守改善7.6750%/8.6660%，但65 max140.311–168.869ms、100 max171.659–196.602ms仍FAIL；2人mean保守-2.352%、16人仅0.010%，不能称全部功能极致完成/全局P99改善。相比前64 coalescing负结果，128条件下端到端平均收益确实复现，但不是同一次64到128+合并组合直接ABBA，不能把两轮百分比合成为严格全局收益。

100人33消息窗口的确认UPDATE数量OFF3266/3267（A1计数与affected差一条，全窗口计数边界/并发背景允许，不能冒充逐条唯一计数），ON分别单条38+批次323=361、单条44+批次311=355，总数减少约88.95%/89.13%；所有最终逐人状态3仍由每条actualMID记录独立校验。ON带来323/311批次SELECT，不能把UPDATE减少当总SQL减少或RPC数量减少。普通durable SELECT窗口4917/4917次，均值0.567/0.595ms；OFF4917/4916次、0.702/0.740ms。窗口redo fsync计数1664/1669→500/494、rowlocktime3891/3773→507/534ms；这些是全库累计窗口增量，不能认定每条请求一次fsync或直接重建关键路径。

偏置100人ACK样本OFFqueue82.62/82.41ms、Get18.02/17.71ms、Confirm24.86/25.99ms、task42.94/43.84ms；ONqueue92.57/92.27ms、Get10.28/9.25ms、Confirm34.01/35.74ms、task44.37/45.07ms。因此数据库写负担减少，但同步确认调用墙钟没有同步改善，合并等待/后续排队仍存在；不要把SQL减少88%宣传为ACK速度快88%。peer Get约6.76/8.66ms、CPU约0.45/0.44ms，任务结束到用户空间观察的原采样也已保存。所有限频<=8/s阶段是偏置样本，不能把不同阶段均值相加、当总体P99，仍需同调用客户端/服务端/Acquire/PING/Query/Decode关联边界定位RPC残差，不能只凭墙钟认定网络/SQL唯一根因。

只读资源抽查：vmstat连续4当秒si/so均0，CPU idle90/89/94/83%；memory PSI avg10 some/full0。宿主15:49:08UTC单点CPU38%、可用6852780KiB；另一个非同步点CPU13%。不支持持续换页或全局CPU打满作为本轮唯一解释，也不能推出长期充足/单线程没有调度等待。未清内存/缓存、停游戏/Python/其他应用或改主机设置。

本次三组控制共1584消息（含warm144，measured1440）、70884实际收件及确认、12完整链648操作444断言正确。测量完整性新增25checks在本轮第一项实测，后续复用同SHA；每case捕获记录数与落盘一致，无buffer失败。每份阶段原raw、SQL窗口、SHA/恢复/Git审计均保留。结果见benchmark/local_capacity/results/group_confirm_batch128_20261006.json；完整本轮公开证据用export_final_group_optimization_evidence_20261006.sh封存，故意损坏native样本按两精确SHA原样保留，私有配置/Env/inspect/密钥/二进制排除。

当前建议顺序：①在Gateway→Message Get调用增加严格默认OFF、指定收件人、同MID/UID/request_id哈希关联的client/server/repository边界，量化RPC残差和超时预算；②据证据调整同步串行/批量取数或调度，保持同delivery identity peer/ACK FIFO、auth/durable/lease；③同版本小群/大群混合、公平/热点/过载/故障/长稳态后才接受batch128+coalescing；④当前1万指定mix可保留，2万FAIL与5万全功能、文件并发/离线/AI闭环目标仍OPEN。尚无所有功能极致性能已完成的证据。
