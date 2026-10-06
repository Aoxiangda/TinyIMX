# 全部功能优化清单与会话未读批量候选（2026-10-06）

当前状态：源码/API候选，构建/真实Redis语义与性能对照NOT_RUN，未部署。此前10k登录保持P99上界68.4ms仅单次诊断；私聊10k150/s延迟门槛仍FAIL。全功能10k/20k/30k/50k验收均未完成。

本轮明确问题：HandleConversationListRequest最多50条，每条GetPrivateUnread触发独立pool Acquire/PING及GET，N条需2N次网络命令。权限边界来自MessageRpcClient同actor/顺序/唯一peer/limit校验，不能绕过。新增GetPrivateUnreadBatch仅读取已授权peer，最大50，保持输入顺序/逐项typed状态/error/count，包括invalid/missing/wrongtype/非法int64/重复peer；空/allinvalid/oversize没有I/O，健康PING保留一次。固定Lua逐key pcallGET返回标签+原bulkstrings，禁止Lua tonumber避免2^53/int64取整，复用原from_chars解析，不写key/续TTL/delete。当前Redis standalone；不声称Redis Cluster跨slot支持。

Gateway接入将在实际API验证后进行，用默认OFF开关在同ELF上OFF/ON比较，原登录/会话fence/取消/timeout/列表大小/排序/未读零fallback/私聊权限和持久化语义保留；必须检验已有跨功能操作链与真实列表延迟，不能只报native组件收益。批量快照在当前读取时点一致，并不代表与MySQL列表跨存储原子一致或projection无延迟。

全部功能按真实33类Gateway入口跟踪：
- kLoginRequest
- kChatMessage
- kChatDeliveryAck
- kGroupMessageDeliveryAck
- kGatewayForwardChatRequest
- kGatewayForwardGroupMessageRequest
- kReadRequest
- kHistoryRequest
- kConversationListRequest
- kUserProfileRequest
- kFriendListRequest
- kFriendRequestCreateRequest
- kFriendRequestListRequest
- kFriendRequestAcceptRequest
- kFriendRequestRejectRequest
- kCreateGroupRequest
- kGetGroupRequest
- kUpdateGroupRequest
- kDisbandGroupRequest
- kJoinGroupRequest
- kLeaveGroupRequest
- kInviteGroupMemberRequest
- kKickGroupMemberRequest
- kSetGroupMemberRoleRequest
- kSetGroupMemberMuteRequest
- kTransferGroupOwnershipRequest
- kListGroupMembersRequest
- kListMyGroupsRequest
- kGroupMessageSendRequest
- kBeginFileUploadRequest
- kGetFileUploadSessionRequest
- kCancelFileUploadRequest
- kHeartbeat

另外包含文件流续传/完整性、MCP八个域工具、AI Ollama/tool loop、Outbox/MQ/未读投影、TLS/热点/慢消费者/过载恢复/soak。状态按功能正确性、正常性能、混合交互、故障恢复和四个在线规模分别记录；尚未执行的格子明确NOT_RUN，不以49次操作代替万人功能频率。

只读预检：Git764dbd9、19个服务恢复状态/health、源码/链接输入/Redis standalone/内存约8GiB核对。Ubuntu127.0.0.1:11434实际/api/tags=200，已有qwen2.5:7b；Windows没有监听/ollama进程，AI原配置host.docker.internal/qwen3不能因此称可用。下一步需核对AI容器到实际模型端点的可达性与真实推理，不能用假provider代替AI验收。首预检RedisCLI凭据来源错误导致AUTH失败，仅工具失败；新attempt2使用既有配置在RAM中独立RESP认证，未改产品/Redis配置。

自有API测试与组件ABBA：pool1/4分别全新namespace，正常/0/int64max/2^53+1/leadingzero/negative/overflow/empty/suffix/whitespace/plus/NUL/wrongtype/missing/invalid/重复、50/51/空/null/uninit/ownshutdown、100并发mixedbatch和200次原子双计数快照。仅own新key TTL300，No DEL/FLUSH/CONFIG/系统故障；原bytes/TTL验证。单ELF原50单读与batch50各100page×ABBA保留每页时间、CPU、全部count，closed-loop/native组件不能作为RPS或Gateway端到端成绩。所有错误/failed/live/源码/ELF保留，不能弱化密码校验或ACK持久化。

Redis官方关于Lua原子执行及pcall错误隔离：https://redis.io/docs/latest/develop/programmability/eval-intro/ 。脚本执行会占用Redis执行线程，因此硬限50且无扫描/写入；本地压力测试仍需验证对共享实时链路的实际影响。
