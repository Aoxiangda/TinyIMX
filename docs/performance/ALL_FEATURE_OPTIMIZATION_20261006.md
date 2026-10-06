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
