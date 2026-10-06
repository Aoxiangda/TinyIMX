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
