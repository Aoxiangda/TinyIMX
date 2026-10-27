# 企业工程审查：性能成本、外键、持久化和可运营性

更新：用户已明确选择应用层维护关系、业务不使用外键的规范。本项目按此执行，不再把是否采用作为待确认项。无外键实现与真实迁移/仓储/native 验证见 [PERF-146 改造记录](FK_FREE_CHANGE_20261020_R146.md)。下文的 FK 代码/运行库描述是 R144 审查时快照；原运行库保持保护，当前源码候选已变更。

结论：已获得有效进步，但仍不能认定已经完成生产容量或工程验收。下述6.175/18.882ms和5.441忙核是R134旧工作点；R309同九程序四轮限页对照进一步验证ACK P99下降10.09%/11.87%、忙核下降3.53%/2.43%。R316在10K、1000/s、60s完成60000条全链，ACK P99 24.1472ms、5.772093忙核。固定WuKongIM旧700/s成绩不能直接与新1000/s相除；20K的新匹配点仍待完成。性能与工程正确性分别验收，见[当前容量及负结果](CAPACITY_PROTOCOL_20261021_R316.md)。

## 企业外键规约的适用范围

阿里巴巴公开 [SQL规约](https://raw.githubusercontent.com/alibaba/p3c/master/p3c-gitbook/MySQL%E6%95%B0%E6%8D%AE%E5%BA%93/SQL%E8%AF%AD%E5%8F%A5.md) 采用应用层维护引用关系、避免数据库外键和级联的政策。这是一套可采用的工程约束，不能推广为所有企业的通用事实。若 TinyIMX 选择这一政策，当前带外键的表结构明确尚不符合该项政策。

MySQL官方 [外键说明](https://dev.mysql.com/doc/refman/8.0/en/create-table-foreign-keys.html) 将其作为完整性约束，并要求支持索引；[锁说明](https://dev.mysql.com/doc/refman/8.0/en/innodb-locks-set.html) 指出检查涉及记录的共享锁。Vitess官方 [24.0外键文档](https://vitess.io/docs/24.0/user-guides/vschema-guide/foreign-keys/) 列出分片共置、在线DDL、级联与复制工作流的限制，也提供禁止外键的模式。这些支持评估成本与部署约束，不能证明去掉外键必然解决当前CPU差距。

仓库总建表脚本的静态盘点为20条FK、4条DELETE CASCADE、20条UPDATE CASCADE；native实验脚本另有2条RESTRICT FK。随后已在Peer实验结束、原服务恢复后只读核对实际catalog：源库28条FK，其中26条业务FK、2条旧RTT测试表FK；相对总脚本多出的6条业务约束属于文件域迁移。native r140引用表实际为2条RESTRICT FK。全局/会话外键检查均为1，flush_log_at_trx_commit=1、sync_binlog=1；源消息/事件行数2267400/1698446不变。[运行库catalog](../../benchmark/results/peer-20261017/PERF144-enterprise-runtime-audit-r1/fk-catalog.json)。未执行DDL或关闭检查。

## 已查到的本项目事实

|问题ID|源码与现状|工程判断|执行/验收要求|
|---|---|---|---|
|PERF-144.1 原SQL表的引用关系|[common/db/schema.sql](../../common/db/schema.sql) 中好友/请求含级联，私聊用户引用 `DELETE RESTRICT/UPDATE CASCADE`；群等域也有关系约束|采用应用层关系政策时应分域迁移，不能一条命令关闭全部检查。不同表的级联与限制语义需要分别替代|先记录实际数据库catalog；只在新隔离schema实施候选，原业务及保留测试库不改|
|PERF-144.2 native SQL兼容层|[ownership_schema.sql](../../services/message/local/ownership_schema.sql) 两条 `RESTRICT` FK；[NativeActorReferences.cpp:52](../../services/message/local/NativeActorReferences.cpp#L52) 强校验约束，冷用户写引用登记|这是保留旧删除约束的迁移桥，不是最终高并发无外键数据模型；直接删约束会使当前服务拒绝启动|替代需要用户ID不可复用、用户状态/注销流程、并发发送fencing、恢复对账、所有写入口与数据库权限收敛；不是仅删DDL|
|PERF-144.3 热路径SQL|[MySqlLegacyIdentity.cpp:24](../../services/message/local/MySqlLegacyIdentity.cpp#L24) 每条调用SQL CID排序规则及用户存在性；权限仍每条查权威关系SQL|这是明确的热路径依赖；“去掉FK”不会自动消除这两类应用查询|优化规范化/权威版本与失效机制，验证Unicode/PAD SPACE幂等及并发撤权；记录查询次数、等待、CPU和故障恢复|
|PERF-144.4 数据所有权与扩展|[NativeActorReferences.cpp](../../services/message/local/NativeActorReferences.cpp) 用SQL锁/owner指纹保护本地数据；[NativeMessageRuntime.cpp:39](../../services/message/local/NativeMessageRuntime.cpp#L39) 仍依赖共享SQL用户/引用表|当前native是单owner迁移候选，不能把它描述为已经可任意扩展的无状态Message集群。五个进程域也不等于五个独立数据边界|后续如要多owner或跨机恢复，需要明确分区路由、写者fencing和迁移/恢复；现阶段保留单owner失败关闭保护|
|PERF-144.5 提交和ACK边界|原SQL是消息+事件同事务；native是消息/索引/事件同本地同步批次，之后发布MQ。Peer也有同步本地提交|事务/原子提交服务于一致性，不是因为实时系统就不需要事务。删除事务或提前ACK会改变成功含义|任何优化必须通过持久成功后崩溃、重复请求、异常重试、事件恢复、接收确认和权限合同；SQL/native架构变化必须披露|
|PERF-144.6 CPU成本|健康诊断broker+proxy约1.7核，Message约1.5–1.7核，另有projector/Redis/Gateway和SQL；[AckBatch实现](../../services/eventing/rocketmq/RocketMQSimpleConsumer.cpp#L225) 仍逐receipt调用SDK asyncAck|不能只优化ACK P99或用更多线程换成绩。后台链需要减少真正的每消息工作；方法名Batch不代表一次原生批RPC|进一步核对MQ请求/确认/快照次数及实际SDK能力，再验证部分失败、重试、停机、重放和完整收尾；不可暂停后台换低CPU|
|PERF-144.7 有界性与可运营性|已有有界执行、顺序键、receiver ACK隔离、epoch、权限检查、失败关闭和部分真实终止测试；现有完整点仍仅60s|这些是工程基础；尚不能等价于稳定容量、节点失效恢复和上线就绪|继续阶梯容量、长稳、慢依赖/断连、恢复与回滚；同时看队列增长、拒绝率、P99、CPU/内存、磁盘和完整后台欠账|
|PERF-144.8 内部服务身份边界|[MessageServiceServer.cpp:68](../../services/message/server/MessageServiceServer.cpp#L68) 当前使用insecure gRPC；[UnreadSnapshotRpc.cpp:25](../../services/projection/unread/UnreadSnapshotRpc.cpp#L25) 只读入口验证参数、取消及返回数据，未见该方法中的服务身份校验|当前实验依靠隔离后端网络，未开放该RPC为公网能力；这不能代替生产部署的服务身份及最小权限。参数正确与调用方可信是不同要求|盘点所有内部RPC的认证/授权，结合部署边界做mTLS或受控服务身份、证书/凭据轮换与拒绝测试；不能只给一个辅助接口加可伪造caller字符串|

## 不能把FK当作当前主要瓶颈

三轮健康native诊断中，暖用户引用登记平均约0.007/0.005/0.005ms，多数已在内存中登记；冷路径仍发生5000次两用户引用写入。身份SQL桥和权限SQL则仍逐消息执行。上述采样表明应优先减少热路径查询及后台工作，并不证明FK没有成本。不能把冷路径插入耗时与暖路径占用混为一谈。

`foreign_key_checks=0` 也不等于完成应用层完整性设计。MySQL官方说明重新开启检查不会自动扫描此前写入的不一致数据，因此这不作为本项目压测优化手段。

## 同步提交边界也已继续穿透源码

Peer发送主路径并非只入队就返回ACK：`Store.AppendMessages`等待`ProposeBatchUntilAppliedTimeout`；channel storage的`AppendLogs`直接写消息KV，随后`CommitWait→pebble.Sync`完成。channel的`Apply`本身为空操作，应用进度再唤醒等待者。已将7个文件逐字节与固定官方ZIP对照并冻结：[提交链源码与SHA](../../benchmark/results/peer-20261017/PERF144-peer-applied-commit-source-r1/result.json)。

因此不能把Peer更快简单解释为“它没有同步持久化”，也不能把另有8个slot日志库解释为每条消息重复写了第二份日志。本地消息日志与消息存储共享该AppendLogs路径。当前Tiny普通接收确认仍有额外持久状态，其语义差异单独披露；两边都还没有本轮真实断电证明。

消息与事件原子提交的动机可参照AWS的[transactional outbox模式](https://docs.aws.amazon.com/prescriptive-guidance/latest/cloud-design-patterns/transactional-outbox.html)：目标是避免业务写入与事件发布分裂。采用应用层引用管理时，这一提交边界仍需要保留。

内部服务身份后续按[gRPC认证接口](https://grpc.io/docs/guides/auth/)设计通道身份及方法授权；当前隔离实验的明文网络不能代替生产网络边界。运行验收同时观测延迟、流量、错误和饱和度，参照[Google SRE的监控原则](https://sre.google/sre-book/monitoring-distributed-systems/)，并针对消息系统补充持久事件、消费欠账和恢复状态。

## 每项优化的共同门槛

1. 冻结原实现和提交/权限语义，写明变更边界；默认开关保持可回退。
2. 在新隔离数据域验证正常、重复、乱序、撤权、注销/并发发送、错误回调、重启、崩溃和停机；适用的合同必须真实通过。
3. 同配置同负载做正反控制；成功数、持久化、实际MQ、Redis与重试队列一致后才比较性能。
4. 同时要求延迟、整机CPU、队列稳定及依赖成本合理；负结果保留，未复现的改善不推广。
5. 给出部署、迁移、监控、恢复和回滚材料，再决定是否改变原默认实现。

本轮仍继续固定WuKongIM的8/1/1/8存储布局对照，用实测决定是否值得引入多库；不是按名词数量认定“企业级”。外键替代与共享SQL解耦属于单独的架构候选，原数据库未修改。

## 2026-10-21 工程检查继续项

固定 Peer 的分片四轮对照已完成；无外键替代代码、28→0 的隔离迁移、真实旧数据原地升级及后续完整业务链已验证，原业务库仍按保护要求保留。应用层引用检查、唯一约束和原子提交没有为性能而取消。

新增 [R180 有界原生未读批量 RPC](NATIVE_UNREAD_BATCH_RPC_20261021_R180.md) 已通过九产品构建、25 项真实网络契约及对应 sanitizer、22 项真实持久 owner 和 46 项既有原生契约。审查中发现的旧服务回退超时 N 倍放大已修正为共用整批截止时间，并用慢旧服务验证整批失败与有界调用数。新开关默认关闭；内部调用身份/mTLS、完整负载 CPU 收益、持续容量和长时间故障恢复仍未由本次通过替代。

R188 的 SDK 日志候选没有用禁用所有日志替代问题处理：延迟求值交给原日志后端，运行级别、backtrace 和异步临时对象行为分别验证，C++11 及两个原 fmt 组合通过。R191 完整候选库构建、原依赖隔离及九角色加载通过，原 SDK 未替换。原库与新库默认 OFF 字节不同，后续要分清原库回归、同候选 OFF/ON 因果对照和实际 IM 完整链；当前没有生产部署或性能达标结论，见 [R188 记录](SDK_LAZY_DEBUG_20261021_R188.md)。

R192r3 进一步通过四组真实 MQ、共 128 条消息的限定验收，覆盖正文与键、逐条回执、漏 ACK 后重投、停止后失败和两个实际调试格式化路径。两次夹具失败分别定位到代理没有公开映射 8081、投影器启用依赖配置缺失；新范围在创建前先校验配置，没有改产品校验或重置消费位置。R205 的 18 份结束 raw 归档恢复后回收恢复磁盘，所有程序和 138 缓存继续保留。R209 正常重启后原设置及 510 个程序/原生文件复核通过；R210 宿主内存仍低于原门槛，R213 自动审批拒绝正式 A/B，尚无新的整机性能成绩。这些限定验收没有替代全链顺序、权限、持续容量或长时间故障验证。

R221—229 新增 ACK 线协议扩展遵守同一要求：原 SDK/对象布局/虚表及共享回调队列保留，逐条解析失败不误报成功，共享网络截止时间，Main 缺能力时在创建 client 前显式拒绝，模糊失败不静默回退重发。SDK/Main 两层受控真实 gRPC、两组 OFF/两组 ON 共 128 条真实 Broker、超不可见期后无重投、停止后失败及 18 次角色加载通过。原库、九程序和现有数据保留，默认 OFF；并发 Stop 不属于支持合同，网络截止时间不等于不受回调队列影响的硬 API 时限。实际 Proxy 启动值 false 已独立核实，客户端合并与 Broker 内部批量没有混为一项。见 [逐项证据与推广门槛](MQ_WIRE_ACK_BATCH_20261021_R218.md)。

R235/236进一步实际验证服务端批量：正常32条的Broker请求32→3，但坏回执导致失败范围从1扩大到16条。全部恢复不等于完整业务投影幂等已验收；需追加坏项位于中间、真实projector重复处理及正式负载等验证，默认继续false。R237按原始pcap重组核对请求，明确字节命名长度差异，并拆清系统心跳发送；不把包数、调用数或功能仪器配置当整机收益。细节见 [服务端分支及取舍](PROXY_ACK_BRANCH_20261021_R235.md)。

R243把MQ重投验证推进到真实原生owner/relay→RPC→Redis投影，已读持久推进后重投无状态回退；身份仍为fixture，生产权限切换及长时故障未替代。R247观测接口明确新API进程级范围、能力/可用性和生命周期；R248旧SDK兼容及逐条失败传递通过，未引入ACK热路径额外计数，默认不变。原业务数据库未切换，引用一致性和事务要求继续保留。详见 [恢复与观测验收](NATIVE_ACK_RECOVERY_STATS_20261021_R243.md)。

R255/256实际镜像复验纠正了容器加载证据范围：镜像LD_LIBRARY_PATH优先选择/opt/tinyimx/lib旧SDK，R247的6项MQ loader日志（包含old/wire标签）均解析到该旧库。此前18项检查仅证明依赖可加载，不证明新SDK选中。显式优先路径、三个MQ角色的只读挂载与实际选中SHA已在9项镜像对照通过；原/新库6项正向控制符合。宿主侧显式候选路径的功能实验仍有效。独立SDK正式控制器还需接入绑定并核对运行映射，未启动完整性能轮。详见 [实际镜像路径缺陷及修正](SDK_IMAGE_BINDING_20261021_R255.md)。
