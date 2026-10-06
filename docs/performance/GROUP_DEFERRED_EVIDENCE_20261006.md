# 群测量证据移出关键路径（2026-10-06）

状态：新增源码准备，尚未运行、不能声明性能接受。上一离线证据100人send后累计JSON重写47–61ms、99条合成timeline约11.3ms，但不能直接扣历史数字或认定全部服务端问题已解。原cross_feature_actor和确认控制器SHA保持，保留历史结果。

DeferredTimeline只作测试采集，不修改产品/协议/ACK/期限。emit记录原capture monotonic、UTC、事件、所有body，深拷贝防后续变更，不做文件I/O或JSON编码；有界8192条、8MiB保守reservation（不是精确RSS），超限显式FAIL，不丢弃。无额外线程/GIL写入竞争。每次测量实际wire完成及逐人SQL3之后才持久化新immutable checkpoint；累计all-requests只在安全边界保存。最后合成原timeline格式，并核对chunk SHA和总record count。部分写失败原partial文件/内存记录保留，无自动重放，显式恢复证据也不能判成功。原fsync合同未加未减。

原生验证覆盖热路径无I/O/序列化、UTC/mono/uint64/Unicode与深拷贝、FIFO/count、上下限、部分写失败无重放与恢复、检查点篡改检测、关闭后拒绝。测试只创建本任务新目录与失败文件，保留所有内容。

实际对照预定OFF同步/ON延后/ON/ OFF，服务端同818bea35 Gateway和645e9cc Message、完整Env固定，确认coalescing严格0，原message-runtime1（实际每Gateway16worker），route/completion/claim/partial/recipient/defer/commitwake固定1；4自有群2/16/65/100，100实际连接、每组3warm30测量，四原54op37assert链，真实文件字节/权限/幂等保持。原3秒ACK/ALL、SQL8秒观察和身份/SQL3判定不变，所有慢/失败样本保留。完成后原5c2645/be8及全Env/Health/HostConfig/Mounts精确恢复，其他16/配置/持久化1/1/1/0/0及宿主应用保持。30样本不估P99，不当万人容量。

若采集改良后仍慢，必须关联同MID的RPC入队/服务器处理/SQL/发送/接收/ACK阶段再选产品优化。20k混合/50k全部功能、文件并发/AI/故障/多端/长期稳态仍OPEN，不把测量优化冒充服务端收益。准备第一次精确缩进替换失败发生在guest变更前，原失败与首2新文件保留；继续按真实行缩进处理，不更改原代码。
