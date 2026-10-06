# 群投递租约领取 SQL 批量候选（2026-10-06）

状态：SOURCE_ONLY，尚未构建、部署或达到性能验收，原运行镜像保持不变。

按收件人排序及同批先派发后完成降低排队，但真实四轮 65/100 人群仍超 100ms；64 收件人领取 RPC 约 60 多毫秒。源代码仍逐条 UPDATE 租约，SELECT、64 UPDATE、COMMIT 后才派发。本迭代只减少这些已锁定记录的 UPDATE 往返。

默认关闭开关 TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE=1，严格只接受 1。在线和离线领取均使用原 SELECT FOR UPDATE SKIP LOCKED、排序、过滤、上限256、类型验证；仅将已锁定 PK 合并为一次 UPDATE，要求 affected_rows 等于记录数。保留 PING、原 COMMIT、租约、重试和 ACK 赢得状态不被完成 RPC 回退。单批 NOW(3) 时间一致，事务提交前不返回工作。

待执行真实隔离 MySQL OFF/ON/非法开关 pool1/4：精确行集合/排序/空批/256上限/uint64/引用反斜杠/在线租约/离线原状态/并发不重复/SKIP LOCKED；SQL故障、affected_rows不匹配、提交前故障回滚；真实提交后模拟返回错误不派发和租约回收。wrapper仅测试二进制，提交后错误为返回值模拟，并非实际网络断开。全部在六个事先不存在的自有 schema，不复制或修改生产数据。

所有失败、构建中间产物、原记录与测试 schema 保留。同 ELF 四轮真实消息、确认、功能链和 10k–50k 所有功能尚需验收，不得以 SQL 指令减少替代整链路验收。
