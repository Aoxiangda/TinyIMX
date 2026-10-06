# 群投递完成批处理候选（2026-10-06）

状态：仅本地源码准备；未在guest应用、编译或部署。群部分drain真实ABBA确认64批完成RPC308–319ms，约整批75%；100人全收件约487–531ms仍FAIL。下一修正明确减少每批64次完成的租借/PING/SQL/RPC必要串行，不用增加批次掩盖。

此步只增加独立仓库API，不改原single completion或任何现有调用。1..256 unique uint64 MID/UID，每条原token/status=1 fence、同原status1/2、gateway/error最大128bytes与retry600000；一次Acquire保留健康PING，一条atomic UPDATEJOIN按所有精确PK配对，每条gateway/error/retry独立，空字符串同原NULL，NOW3按新批实际执行时间。没有BEGIN/额外线程/缓存，原autocommit和持久化保持。ACK3/新token/缺行允许成功affected0，不要求受影响行=输入数量；不能以数量不匹配回滚ACK获胜。

所有参数在Acquire前校验，重复pair拒绝，联合SELECT明确CAST uint64为UNSIGNED避免double/signed截断。单语句失败整批回滚；真实语句提交后模拟错误保持StorageError，重试同token为no-op，原恢复和lease不放宽。模拟wrapper不等同真实断网。原单条的逐行部分成功边界是历史行为，新batch原子边界需全链和故障评估。

新增真实ownSQL测试预计覆盖混合offline/提交状态、引用反斜杠字节、retry原值、ACKwinning/newtoken/no-op、全部无效参数、真实触发器中途SQL错误原子回滚、模拟提交响应丢失、uint64、256/257、20次真实并发ACK、8批64行的实际UPDATE/PING次数与完整durable状态。仅新absent自有数据库夹具，数据不删除；原服务没有调用此API。后续RPC/proto与Gateway接线必须单独审计，并处理对象布局和旧主程序重编范围；源候选绝不当已接受性能。
