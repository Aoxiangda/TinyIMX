# 未读投影的逐条 RPC 与有界批量候选（PERF-180）

2026-10-21。状态：代码、真实网络契约、内存检查和原生持久化回归已通过；默认关闭，尚无完整负载性能收益。原业务库、九个保护测试库、所有现存容器、快照及 binlog 保留。

## 具体差异与改动

TinyIMX 的 `UnreadProjector::ProcessBatch` 已有批量接口，但原生存储路径的 `UnreadSnapshotRpcClient` 没有覆盖 `LoadDialogSnapshots`，因此继承默认循环：N 个对话仍发 N 次 `ReadDialogUnreadSnapshot` RPC。已有 MySQL 批量查询不能证明该原生路径也在网络上批量读取。固定 WuKongIM 的同进程存储/元数据组织提供了减少跨进程工作量的对照方向，见 [固定版本源码与分片实测](SOURCE_GAP_REVIEW_20261020_R141.md)；不能据此把两者所有 CPU 差距归为这一处。

候选增加 `ReadDialogUnreadSnapshots` 内部 RPC，最多 256 行、8 KiB 请求。保留原有方法和字段，将新方法追加到原内部服务；服务端一次接收有序对话列表，调用持久源读取，再返回同序结果。**只合并网络调用，原生持久源目前仍逐对话读取**，没有宣称一次数据库扫描或跨用户原子快照。

启用需要同时满足：

- `TINYIMX_NATIVE_PRIVATE_STORE_ENABLE=1`，且指定原生 owner 端点。
- `TINYIMX_UNREAD_SNAPSHOT_BATCH_ENABLE=1`，并使用独占的 projector writer，非 shadow 模式。
- `TINYIMX_NATIVE_UNREAD_BATCH_RPC_ENABLE=1`。此新开关默认 0；原单条方法和默认路径保留。

改动后的顺序是：接收 MQ 批次 → 原有接收用户分条带锁 → 一次快照 RPC → 验证全批结果 → 原有 Redis 投影 → 仅确认成功投影的事件。没有改变消息 WAL、提交 ACK、接收者确认、CID/MID、成员权限或 MQ receipt 处理。当前 RocketMQ `AckBatch` 仍是并行单 entry ACK，本候选没有把它改为真正的 MQ 线协议批量确认。

## 失败、兼容和停机边界

服务端先验证全体用户/对话和请求大小，再访问持久源；先验证全部返回行的数量、位置、用户、对话及未读数关系，再构造成功结果。客户端独立复核上述约束。错行、缺行、负数或不一致总数、异常、不可用及超时都返回明确失败，不得用成功的空快照清除缓存。

旧 owner 对新方法返回 `UNIMPLEMENTED` 时，允许回退到原单条 RPC。审计发现若给每条回退重新分配完整超时，会将整批最坏等待放大 N 倍，已修正：新批量调用和所有兼容回退共用一个截止时间。预算耗尽或任一回退失败，整批失败，已读取的部分结果不作为成功暴露。普通的 `UNAVAILABLE`、数据错误和超时不触发兼容回退。

这只是内部数据一致性校验，不是调用者身份认证。现有隔离实验仍使用明文内部 gRPC；生产 mTLS、服务身份和方法授权仍按 [企业工程审查](ENTERPRISE_ENGINEERING_REVIEW_20261020_R144.md) 单独处理，不能把本次通过描述为生产部署完成。

## 实际验证

| 验证 | 实际结果 | 证据 |
|---|---|---|
| 全部九个服务构建 | 通过 | `PERF180-native-unread-batch-qualification-r3/result.json` |
| 真实 loopback RPC | 两次各 25 项通过；一批只调用一次 owner RPC，默认关闭时仍逐条调用 | 同上 `native_unread_batch_rpc_tests-*.log` |
| ASan / UBSan | 两次各 25 项通过 | 同上 `native_unread_batch_rpc_sanitizer_tests-*.log` |
| 边界与错误 | 256 行、重复对话、完整 64 位 ID、空/超量/非法输入、未知字段字节预算、缺失/错序/错用户/错误计数、异常、超时、并发调用、旧服务兼容通过 | 同上 |
| 慢旧服务的超时预算 | 单条延迟 80 ms、整批预算 120 ms；整批失败、最多发起两次单条调用、返回不超过 300 ms，普通及 sanitizer 均通过 | 同上 |
| 实际持久 owner | 22 项通过：既有持久写、确认、已读、历史，新增批量读取真实消息/零未读/重复对话，以及 owner 和服务正常关闭后的整批失败 | 同上 `previous-native-owner-RPC-integration.log` |
| 既有原生持久化合同 | 46 项通过 | 同上 `previous-native-durable-contracts.log` |
| 保护项 | 2,292 个容器配置/镜像/运行状态、原业务和九个保护库保持原样；R175 冻结程序未变 | 同上 |
| 真实角色加载与 E 盘恢复 | 九个产品在原角色镜像中 `ldd -r` 通过；2,375 个归档成员在 E 实际独立恢复，逐个匹配哈希 | `PERF185-batch-candidate-E-seal-r1/result.json` |

所有证据的公共前缀为 `benchmark/results/peer-20261017/`。R180 初次 sanitizer 构建因缺少源码 include 路径失败，修正 CMake 后在新 r2 空间通过；实际 owner 补验初次使用 Windows 路径分隔符，写入或构建前失败，改用 POSIX 路径后新 r2 通过。旧日志和原始状态保留。r3 是共用回退截止时间修正后的最终验收，不混用 r2 产品哈希。

当前候选及精确源码的 E 盘归档为：

`<LOCAL_EVIDENCE_ROOT>/native-unread-batch-20261021-r185-r1/runtime-and-exact-source.tar.gz`

SHA-256：`1b2a3314e610a3ef26747e2e8acad4402bdd72a7cce4ad68f390c2043d075096`。虚拟机上的构建路径仍是可变路径；不可变备份在 E。没有删除构建程序，也没有复制额外一套完整运行程序挤占虚拟机空间。

## 完整性能验证仍待执行

R175 按准入空位限页候选已冻结，先完成它的完整系统对照，再给本候选单独建相同九程序、只改变 batch RPC 开关的控制组。批量投影开关必须在两组固定一致，不能把既有 ProcessBatch 和新线协议批量的收益混为一项。要求全量消息、持久状态、MQ、Redis、失败/重试、零 FK/零孤儿、正常停机及资源门槛同时通过，才比较 CPU 和延迟。

本次 R175 OFF1 在连续 600.64 秒预检中可用内存最高仅 7.234 GiB，未达到 8 GiB 门槛，服务和客户端均未启动；独立核验 r156/r157 四类表全 0、所有原容器和冻结程序未变，见 `PERF181-r175-preflight-zero-traffic-r1/result.json`。这是资源拒绝，没有新的 42,000 条负载成绩。当前不能据 RPC 次数减少宣布整机 CPU 已下降、容量达标或已追平 WuKongIM。
