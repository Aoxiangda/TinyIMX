# 群测量证据移出关键路径（2026-10-06）

状态：新增源码准备，尚未运行、不能声明性能接受。上一离线证据100人send后累计JSON重写47–61ms、99条合成timeline约11.3ms，但不能直接扣历史数字或认定全部服务端问题已解。原cross_feature_actor和确认控制器SHA保持，保留历史结果。

DeferredTimeline只作测试采集，不修改产品/协议/ACK/期限。emit记录原capture monotonic、UTC、事件、所有body，深拷贝防后续变更，不做文件I/O或JSON编码；有界8192条、8MiB保守reservation（不是精确RSS），超限显式FAIL，不丢弃。无额外线程/GIL写入竞争。每次测量实际wire完成及逐人SQL3之后才持久化新immutable checkpoint；累计all-requests只在安全边界保存。最后合成原timeline格式，并核对chunk SHA和总record count。部分写失败原partial文件/内存记录保留，无自动重放，显式恢复证据也不能判成功。原fsync合同未加未减。

原生验证覆盖热路径无I/O/序列化、UTC/mono/uint64/Unicode与深拷贝、FIFO/count、上下限、部分写失败无重放与恢复、检查点篡改检测、关闭后拒绝。测试只创建本任务新目录与失败文件，保留所有内容。

实际对照预定OFF同步/ON延后/ON/ OFF，服务端同818bea35 Gateway和645e9cc Message、完整Env固定，确认coalescing严格0，原message-runtime1（实际每Gateway16worker），route/completion/claim/partial/recipient/defer/commitwake固定1；4自有群2/16/65/100，100实际连接、每组3warm30测量，四原54op37assert链，真实文件字节/权限/幂等保持。原3秒ACK/ALL、SQL8秒观察和身份/SQL3判定不变，所有慢/失败样本保留。完成后原5c2645/be8及全Env/Health/HostConfig/Mounts精确恢复，其他16/配置/持久化1/1/1/0/0及宿主应用保持。30样本不估P99，不当万人容量。

若采集改良后仍慢，必须关联同MID的RPC入队/服务器处理/SQL/发送/接收/ACK阶段再选产品优化。20k混合/50k全部功能、文件并发/AI/故障/多端/长期稳态仍OPEN，不把测量优化冒充服务端收益。准备第一次精确缩进替换失败发生在guest变更前，原失败与首2新文件保留；继续按真实行缩进处理，不更改原代码。


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
