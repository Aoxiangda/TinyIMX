# 群消息提交到真实交付的延迟瓶颈（2026-10-06）

四轮真实交互每组1条新消息/3名收件人，严格相同MID/群/发送者/内容：
|case|请求→发送ACK ms|请求→全部收到 ms|ACK→全部收到 ms|
|---|---:|---:|---:|
|A1|23.949403|148.571237|124.621834|
|B1|20.079300|694.784020|674.704720|
|B2|25.722936|449.170399|423.447463|
|A2|23.966386|556.331727|532.365341|
只有4消息/12收件事件，不是P99、不是所有群容量。群SQL JOIN候选ON/OFF切换的结果不能解释这个发送ACK后的延迟，更不能把20ms ACK当作群交付已达标。

实际两个Gateway均启用扇出，未配置RECOVERY_MS，原入口默认1000ms。GroupFanoutCoordinator每次RunOneIteration后固定等待这个周期，新提交没有通知。MessageRepositoryAdapter在一个事务内提交message、全部recipient delivery rows和outbox，成功ACK随后才返回；群投递直接claim这些durable rows，MQ outbox不是这个直接投递的前置。定时等待是明确的机制瓶颈，具体全部时间组成与改善幅度仍由同ELF OFF/ON实测确认。

## 默认关闭的修正
TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE仅精确1启用。Gateway收到非冲突、正MID的持久化RPC成功后给本进程coordinator一个合并通知；在取消检查前通知，已提交后客户端断开仍可交付。不保存消息队列/内容/IDs，仅有界generation与CV。通知不是投递依据，仍执行原claim RPC、唯一租约、跨网关路由、收件ACK及状态fence。
保留原1000ms恢复轮询。提交结果不确定、无通知、进程崩溃、通知丢失均由原durable恢复补偿。满批最多4轮后让出25ms，避免固定1s分批等待；batch64/租约5000/ACK重试3000/失败重试1000/数据库持久化/身份与权限均不变。关闭时原等待路径保持，正常运行无周期缩短。
原生测试覆盖原2个claim/dispatch/complete及非法依赖测试、唤醒睡眠线程、提交在claim期间不丢、2000通知合并、无通知定时恢复、满批原租约完成、prompt stop。待隔离构建/原生/真实群交付控制，不声称已优化上线或万人达标。
