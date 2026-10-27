# 稳定身份信息快速路径：范围、工程约束与验证

本候选只处理 `MySqlLegacyIdentity` 中重复的 CID 排序权重及正向用户存在查询，不处理认证、账号状态、好友关系、群权限、在线路由、SessionEpoch 或投递权限。配置 `immutable_ascii_identity_cache_enable` 默认 `false`，位于独立 native owner JSON；现有部署与 a70 消息批提交实验不自动启用它。

## 已核实问题与实现

PERF-601.1：原 native 消息路径每次租借 SQL 连接、健康检查，再查询 `WEIGHT_STRING` 与双方用户 `EXISTS`。R575 同 MID 的 warm 样本中 identity 平均约 0.893 ms；这不是全量 ACK P99，也不能把 R581 整个 MySQL 容器的 0.5865 忙核都算作此查询。固定 WuKong 本地追加路径不逐条执行这项旧 MySQL 排序兼容查询。候选采用其减少稳定元数据重复读取的方向，没有复制或修改其代码。

启动仍要求实际 MySQL 8.0.40、`utf8mb4_unicode_ci`、`PAD SPACE` 与原 SQL 消息列规则一致。开启后由真实 MySQL 读取 65 个字符 `0-9 A-Z a-z _ - :` 的权重；只接受非空、非 SPACE、单个四位十六进制 primary weight。已知双方用户且 CID 全部属于这个字符集时，拼接启动时取得的权重。空间、Unicode、其他标点、未知用户仍走原 SQL 逻辑。UTF-8、长度、正文冲突与 CID 幂等检查不绕过。

只有 SQL 成功确认存在的用户 ID 能加入正向集合；不缓存不存在的用户，最多保留 100000 个 ID。超过上限后新用户继续走 SQL。集合有共享锁，字符权重启动后只读；不存在逐消息日志。关闭时输出命中/查询次数，便于核对实际路径。默认关闭的构造器保留原查询依赖。

## 一致性和运维前提

`common/db/LogicalReferences.h` 的现有无外键契约是：父标识保留，业务生命周期通过状态变化表达；在线服务不物理删除用户或修改主键。本缓存只复用“这个稳定标识曾由权威确认存在”的元数据，不能拿该命中证明用户当前已启用、是好友或有群历史权限。动态权限仍由原业务链路判断。

物理清除父记录时，必须先停止相关写入、关闭 native store/owner 与所有该身份缓存，执行既有保留/引用核验及清理，再重建缓存和 owner；不能只删除 SQL 行并保持旧缓存在线。历史测试脚本中的直接 `DELETE FROM im_users` 也只能用于其自己的停机隔离范围。候选没有新增在线物理清理能力，也没有宣称用最终一致性自动修复任意物理删除。

缓存中的已知元数据可以在 SQL 暂时不可访问时继续被解析，这与默认模式逐请求依赖 SQL 的可用性行为不同。新消息落盘仍受 `NativeActorReferences::Healthy()`、relay 健康和关闭状态约束。owner 监控为约 200 ms 周期；本候选没有将其变成跨主机分布式 fencing，也没有承诺故障瞬间零检测窗口。正式 native 启动、进程退出恢复和权限验证必须另行通过，不能由缓存单元检查推断。

## 独立产物与已执行验证

- R609-r2：Release 实际 SQL 比较 283086 组，覆盖 65 字符所有单字、双字、三字、长输入及随机输入；Unicode/空格回退、未知用户、非法 UTF-8、8 线程并发和 pool 关闭均检查。改变的 identity 与测试还通过 ASan/UBSan。
- 旧 a70 程序从保留输入重新链接，SHA-256 完全相同。候选 identity 类布局改变，连同 `NativeMessageRuntime` 一起重编译；不能混用旧 Runtime 对象。
- 独立候选 c2：`c2c1f48b32b8170bfd931f2cc947320242dbfb0ccddc8bd3340f24b36b546994`。没有替换当前正式 a70 对照程序。
- R611-r2：真实 identity + SQL owner/reference + native engine 集成，Release 和改变代码/测试的 sanitizer 模式均通过。关闭 pool 并终止该隔离账户自己的 owner SQL 会话后，监控失效；新单条/批量消息被拒绝，消息与 outbox 计数不增加；原 CID 重试返回原 MID；重建 owner 后正文/MID 完整并能继续写入。停机销毁缓存后物理清除无引用的测试用户，新缓存拒绝；恢复原行后新缓存接受。
- R609-r1 使用了重启前的旧数据库 IP，R611-r1 测试脚本使用了错误的账户字段，分别停在连接阶段；失败和修正均保留。没有把这些失败记为产品正确性或性能通过。

R611 是有限组件集成，其 admit 回调仅使用生产中的 owner 健康条款，没有启动实际 Message、relay 或 MQ；不能替代后续实际服务验收。sanitizer 仪表化改变的 identity 和集成测试，其他未改的存储/引用对象复用早先验证过的 Release 对象，不能称全部依赖都经过本轮 sanitizer。

R614 随后完成实际 c2 Message 服务、RocketMQ 与投影器的 cache false/true 两组有限验收：两组均 ready8、消息批提交 false、进度1，每组 1036 条消息/1037 个不可变事件，全部正文/MID/CID/索引、权限、顺序、Redis 重建、实际 Broker 和进程退出后补发核对通过。开启组 warm512 实际命中511次、SQL检查1次、正向用户2个；关闭组仍执行原路径，实际消息提交均512次。这里没有测试过完整 Windows 网关链路或据此宣称CPU、延迟收益。

R614 复用并最终停止原有专属 Redis 辅助容器，全部2641个保留容器及原20个服务恢复，未增加新的保留容器。功能用 projector 为 R360 的768d产物；正式对照仍须使用固定 R175 的145cc产物，不能悄然把功能资格当作相同正式部署。E盘归档117文件实际恢复/哈希通过，SHA-256 `d5be7f05be438b2618c48423d4a0ead91a5c9656f3d8a6f7eaeafb6078abcdbd`。

## 性能接受条件

先完成实际 Message/MQ、权限、CID、投影重建和退出恢复，再使用同 c2 程序只切换 cache false/true 做完整 Windows 负载对照。ready、消息批提交、进度批处理、SDK、SQL 强持久化、Receiver ACK、用户量/活跃比例及拓扑必须相同。保持既定宿主 8 GiB/10 秒启动、期间 6 GiB、guest 零 swap 与磁盘预留门槛。

缓存命中次数、SQL 查询减少、有限突发耗时与组件 CPU 都不是最终收益。须全量核对正文/CID/MID/顺序/权限/Broker/投影，再比较客户端 ACK、投递 P99、整机与组件 CPU。至少复验相反方向；收益不足则默认仍关闭。当前尚未证明最大持续 QPS、小时稳定性、高可用或追平 WuKong。
