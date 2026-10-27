# 从实际调用栈到单项消融：native 待发布事件扫描

当前结论：已定位并实现一个有实测依据的局部优化，默认仍关闭。完整系统的收益需要同一套二进制的 OFF/ON/ON/OFF 验证，不能用局部查询约一千倍的差异声称 IM 性能提升一千倍。

## PERF-148：真实应用采样和完整消息链

固定 R30、8 vCPU/16 GiB、Windows 原生客户端、10000 活跃连接、700条/s、60s、128B。阶段日志全部关闭；稳定负载期间采集25秒 `cpu-clock:u`，结束、恢复原服务后离线解码。1237个样本、丢失0；实际 ELF/共享库符号逐项保存并校验。它是有采样开销的诊断轮：ACK均值6.5667ms、P99 21.2462ms，不替代既有无采样正式结果。

42,000条客户端成功消息、CID/内容/顺序、全部 native 索引与确认状态、42,000条 broker 事件、20,000个 Redis 计数全部一致；无过程失败、ACK失败、Retry/DLQ欠账，没有重放、重置消费位点或修补缓存。[完整正常链门槛](../../benchmark/results/peer-20261017/PERF148-cpu-projection-quality-r2/result.json)。当时2146个容器和原20服务保留/恢复。

自耗样本中 `rocksdb::MemTableIterator::NextAndGetResult` 占18.76%；报告展示的0.5%阈值以上 RocksDB 自耗符号合计50.45%。调用路径可见 `LocalEventRelay::Impl::Poll → LocalPrivateStore::ListPendingEventsAfter → DBIter::Seek/FindNextUserEntry → MemTableIterator`。[原始报告](../../benchmark/results/peer-20261017/PERF148-user-cpu-profile-r1/self.txt)、[调用路径](../../benchmark/results/peer-20261017/PERF148-user-cpu-profile-r1/callers.txt)。

这些是用户态样本的比例，不是整机CPU比例。短命RPC线程可能存在采样覆盖偏差，采样不能解释所有内核CPU。本轮 Message 进程均值1.5325核，其中用户0.9957、系统0.5368；broker/proxy/projector等成本另行计量，不能把它们计入这个函数。[组件观测](../../benchmark/results/peer-20261017/PERF148-cpu-stage-resources-r1/result.json)。

真实采样与精确符号 E 归档：`cpu-user-stacks-20261020-r148-r2/profile-and-exact-symbols.tar.gz`，SHA-256 `0921fbee54c7f66676de43f79b1a5210cee30bce3b461da5aababcc08e1d6b37`。实际独立解包20个文件，所有大小及哈希一致。

## 源码差异与可证伪假设

Tiny 的 relay 在事件页末尾清空游标，后续从 `E/` 起点重新 Seek。已经完成发布的键被删除；在 memtable/SST 仍含删除标记时，空查询仍可能走过这些历史内部键。固定 Peer 的消息路径是就绪批次提交后推进应用进度，它没有 Tiny 这条“本地待发布索引→独立MQ→未读投影”的同构扫描循环。这个差异不能靠照搬 Peer 的函数名解决，需要减少本项目实际发生的重复工作，同时保留后台链。

假设：在已知待发布数为0时，不创建 RocksDB snapshot/iterator，可以消除这部分空查成本。实际整机收益取决于完整负载中空查的频率和其他组件，必须继续测量。

## PERF-149：实现与一致性理由

`ListPendingEventsAfter` 在既有参数校验和 `Call` 生命周期/poison保护内检查原有 `pending_events` 计数。`EventSlot` 在落盘前预留计数，写入失败时释放；发布/隔离在成功移除后递减；重入队在写入前预留；启动从真实 E 索引恢复计数。因此0可以返回一次合法的空查询，正数只代表上界，仍执行真实快照查询。

没有跳过发布、确认、失败重试、隔离、重入队、进度同步和停机。保留原事件通知，后续新提交可以唤醒 relay。增加开始扫描/跳过空扫描两个计数；显式对照开关在停机后输出一次摘要。默认 `TINYIMX_NATIVE_EMPTY_EVENT_SCAN_GUARD_ENABLE` 未启用。

## 实际验证

同一个可执行文件，分别创建相同的500条同步消息并完成发布，随后测2000次空查询：

|顺序|开关|实际迭代器扫描|跳过次数|进程CPU ms|墙钟 ms|
|---|---|---:|---:|---:|---:|
|1|OFF|2000|0|48.459|48.4612|
|2|ON|0|2000|0.045|0.0460|
|3|ON|0|2000|0.036|0.0377|
|4|OFF|2000|0|48.094|48.0954|

这是一项局部机制实验，不是整机吞吐、ACK或容量结果。测试还覆盖新消息在空查后的可见性、隔离后重入队的原事件字节、写入前预留计数为正但尚未提交的并发窗口、非法游标长度及关闭后的拒绝。

原 store/relay 两组契约在 OFF、ON 下的 Release 与 ASan/UBSan 共8组均通过。开启优化后，又实际在写前、同步写后回复前、发布成功但进度未记前、隔离后四个位置终止测试进程，四项恢复全部通过。[资格结果](../../benchmark/results/peer-20261017/PERF149-empty-scan-qualification-r2/result.json)。

保留了两个测试工具假设错误：native 升级测试曾把改变大小写的CID认作完全相同请求，实际既有契约应返回携带原MID的幂等冲突；空查测试曾把允许的短字符串游标当非法，后改用明确超长的192字节输入。均修正测试，没有改变产品语义迎合断言。

## PERF-150/151：全系统对照准备

9个当前服务程序全部重建、在运行时镜像中通过动态链接校验，Message SHA-256为 `c545ebeebbe8ff6d6336cedc1f970ad6630e7493f3dc827ec5d82c1bc0611d89`。所有程序及对应源码2372个文件已在 E 盘独立解包逐项校验；归档 SHA-256 `b87e845737004d2c769899b226b661d81e44db581dbe2a8b15116fc6962baaeb`。

正式对照使用相同9个二进制，四个新空间 r144/r145/r146/r147，开关 OFF/ON/ON/OFF。新空间先执行业务与native的无外键迁移；所有既有库、容器、快照、binlog保留。关闭阶段采样与 CPU 栈采样，保持10000活跃连接、700条/s、60s、128B和原主机/磁盘门槛。每轮仍须完整业务链和后台收尾通过。只有最终比较支持收益才决定是否保留/推广，当前不宣布整机收益。


## 首轮正式基线：OFF1-r2（后续三轮待完成）

使用冻结的9个当前程序、r144独立零外键空间，阶段/调用栈采样均关闭。42,000/42,000条消息成功；ACK均值6.2416ms、P99 19.6481ms，计划发送到投递P99 19.8929ms。整VM平均5.3844忙核，保守观测覆盖59.122s；Windows可用内存最低6.722GiB，guest换入/换出0。

完整native的CID/内容/FIFO/各索引/确认、42,000条MQ事件、20,000个Redis计数全部一致；总外键0、悬空actor引用0。无过程/确认失败、Retry/DLQ、重放或修补。所有2158容器保留，原20服务恢复。[native验证](../../benchmark/results/peer-20261017/PERF151-off1-native-closure-r1/result.json)、[投影及后台质量](../../benchmark/results/peer-20261017/PERF151-off1-projection-quality-r1/result.json)。

实际停机摘要为扫描72,742次、跳过0、pending=0、published=42,000、quarantined=0、success=1。它证明基线确实执行了扫描，尚不证明开启优化后的整机收益。下一项是ON1、ON2、OFF2，同程序及同负载完成完整收尾后比较。


## 空扫描完整链记录与正式性能拒绝（PERF-151/153/156）

四轮共168000条消息及完整后台链通过，相同九个固定程序、零FK新空间、无阶段采样。前三轮正式资源门槛通过，最后OFF因主机最低5.4029GiB低于原6GiB门槛而被拒绝，正式ABBA未完成。全部数据及拒绝见 [比较和失败记录](../../benchmark/results/peer-20261017/PERF153-empty-scan-full-abba-r1/comparison.md)。默认保护开关仍关闭，未宣布Peer追平或容量达标。

最后OFF因控制NIC DHCP失败改走既有VIP执行SSH，压测数据路径未改变。此偏差不支持严格单变量因果结论，后续成组对照统一控制路径。下一项是非空队列重复扫描的有界索引消融：先保持落盘格式/确认边界/恢复路径，验证并发提交、发布失败、隔离重入队和关闭，再测完整系统。结果工具的继承标签和跨试验指针已保留并纠正，实际运行资格以PERF-152逐项证据为准。
