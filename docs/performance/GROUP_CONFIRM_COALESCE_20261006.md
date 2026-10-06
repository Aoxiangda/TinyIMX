# 群收件确认有界合并候选（2026-10-06）

状态：源码和验证用例准备，尚未编译/运行/实际验收。窗口诊断1bcda00：100人每33消息3267独立确认UPDATE，4worker SQL mean6.062ms、8worker11.160ms；更并发时池有少量0槽及slot1.127ms，但不能解释所有RPC等待。既有route/completion已减少fanout批阶段，原单收件确认仍逐个autocommit。实际SQL/行锁/redo/binlog证明具体成本，未认定唯一原因或直接扩大池。

严格默认OFF TINYIMX_GROUP_CONFIRM_COALESCE_ENABLE=1，只改MessageRepository原ConfirmGroupMessageDelivery进入有界caller-led合并；原single-statement方法字节保持。Gateway durableGet在Tracker registered attempt seq之前，原Confirm RPC、认证UID、deadline、协议/application/adapter、TCP/重试完全保持。Repo仅新增非virtual方法，不增加成员/构造/虚表或ABI布局；perrepository弱注册表由活跃RPC临时shared_ptr持有，完成全部调用后无后台线程/状态拥有者，不合并不同repository，不保留pool/data指针异步执行。

每批最多64、待处理256+运行64，空闲首caller收集最多500us；执行原RPC调用线程，FIFO入队，超限StorageError且不触发SQL，不静默回退另一提交路径。批callback异常/返回数不符只唤醒失败，不自动重放可能发生的副作用；失败结果入队前分配，返回move不会抛出。现有RPC server graceful stop/Wait管理调用生命周期，未新增后台shutdown依赖。

一人批保持原Acquire健康PING+原autocommit UPDATE。多人批一次Acquire/PING，BEGIN、按排序复合主键锁定SELECT、原UPDATE SET/WHERE status<>3、COMMIT；锁下精确计算每人original 0/1 affected_rows，重复caller按FIFO第一个1其余0，已delivered/缺失0。每个RPC在同持久提交确认后才成功；COALESCE(delivered_at,NOW(3))、清lease/error、lastgateway/attempt/retry字段保持。不会把总affected_rows发给每个请求。invalid/65请求在Acquire前拒绝；uint64保持整数SQL字符串。批次同一commit时间对应逻辑线性化，不承诺与原单条SQL毫秒timestamp逐字相同。

新增<=8/s数值阶段诊断默认OFF TINYIMX_GROUP_CONFIRM_BATCH_TRACE_ENABLE=1，保存requests/unique/affected/acquire/begin/query/update/commit/total，不记录内容、身份或租约密钥。确定UPDATE error原事务rollback；COMMIT结果不确定所有参与callerStorageError，不自动回放，cleanup失败Close隔离；原后续确认可显式观察已提交状态3并返回0。合并增加事务错误影响的请求数，必须保存失败耦合。

原生验证：caller-led有界/FIFO/overload/异常无重发/错误唤醒/生命周期；真实新建本任务schema、pool1/4、OFF/ON/非法01/0，percaller状态0/1/duplicate/missing/metadata/64/65/整数边界/SQL与触发器原子错误/真实COMMIT后synthetic错误/并发32caller及重复/ACK与completion竞态/关池。SQL形状EXPLAIN必须主键。所有wrapper只在native ELF，运行镜像禁止wrapper；完整封闭旧Message链接只替换Repo TU并保留其他字节，main无需布局变更。先native/旧应用/协议回归、同ELF单变量入口ABBA与全功能链，再10k/20k交叉；组件32并发不代表容量或整体P99。

全部功能目标仍OPEN，20k混合FAIL、50k全部功能、文件并发真实传输/离线恢复/故障稳态/AI原profile及CPU推理待解决。候选未选入运行。全部预映像、代码、命令、失败、原值和restore需要Git保存，不删除其他任务文件/缓存/主机应用。
