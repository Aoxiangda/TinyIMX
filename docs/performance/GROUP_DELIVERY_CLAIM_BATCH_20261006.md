# 群投递租约领取 SQL 批量候选（2026-10-06）

状态：SOURCE_ONLY，尚未构建、部署或达到性能验收，原运行镜像保持不变。

按收件人排序及同批先派发后完成降低排队，但真实四轮 65/100 人群仍超 100ms；64 收件人领取 RPC 约 60 多毫秒。源代码仍逐条 UPDATE 租约，SELECT、64 UPDATE、COMMIT 后才派发。本迭代只减少这些已锁定记录的 UPDATE 往返。

默认关闭开关 TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE=1，严格只接受 1。在线和离线领取均使用原 SELECT FOR UPDATE SKIP LOCKED、排序、过滤、上限256、类型验证；仅将已锁定 PK 合并为一次 UPDATE，要求 affected_rows 等于记录数。保留 PING、原 COMMIT、租约、重试和 ACK 赢得状态不被完成 RPC 回退。单批 NOW(3) 时间一致，事务提交前不返回工作。

待执行真实隔离 MySQL OFF/ON/非法开关 pool1/4：精确行集合/排序/空批/256上限/uint64/引用反斜杠/在线租约/离线原状态/并发不重复/SKIP LOCKED；SQL故障、affected_rows不匹配、提交前故障回滚；真实提交后模拟返回错误不派发和租约回收。wrapper仅测试二进制，提交后错误为返回值模拟，并非实际网络断开。全部在六个事先不存在的自有 schema，不复制或修改生产数据。

所有失败、构建中间产物、原记录与测试 schema 保留。同 ELF 四轮真实消息、确认、功能链和 10k–50k 所有功能尚需验收，不得以 SQL 指令减少替代整链路验收。

## 实际隔离测试与部署前修正

207 项真实隔离 MySQL 检查全部 PASS：OFF/ON/非法值01分别pool1的33项、pool4的36项；六个事先不存在自有schema全部保留。每64条领取UPDATE实测64→1；含ACK赢、SQL故障/affected mismatch/提交前回滚、真实提交后模拟错误不返回工作及原租约回收。该模拟不是实际网络断连，8个批次计时也不是人口P99或线上性能验收。

Message候选image89f74b5152c66ea5d93189f11e39415d2cb38bdde178b2feb31e894c577f2490，ELF30574112f4fba576bb118d94b6f1818e4dfa85c691caa19b2b2650966d48f1c1。只重新编译MessageRepository，原accepted主程序/其他objects/宏日志语义保留，无testwrapper。stopped loader及missingconfig1检查通过，原19实例/配置/持久化不变，尚未部署。

第一次控制助手运行在服务身份前置查找抛StopIteration：使用了猜测的message键，实际Compose/docker为message-service。此时新控制stage尚未创建、无部署或数据变更；19原实例不变。保留原工具和本次错误审计，核验真实键后仅修正工具并完整重跑原本未开始的四轮。性能失败不会被隐藏、重试或放宽门槛。
