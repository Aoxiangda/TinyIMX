# Redis租借阶段诊断候选（2026-10-06）

状态：真实Redis OFF/ON四组共496检查、隔离构建及20k诊断均完成；诊断网关已经停止，两网关恢复已接受a2bb镜像及原环境。诊断轮延迟FAIL，未接受为性能版本。

动机：20k指定mix仅延迟FAIL，同MID19慢样本permission/route/unread和仓库前后有多段等待，原0条pool/CPU样本不足以区分连接slot、健康PING、命令处理或调度。内存8.165GiB可用，不以清缓存或关闭游戏解决。完整证据见MIXED20K_BOTTLENECK_AND_NEXT_20261006.md。

新增TINYIMX_REDIS_ACQUIRE_TRACE_ENABLE严格1才启用；默认OFF不调用新增观察时钟/线程CPU。开启时Acquire仅选至多每125ms一个数值样本，记录mutex wait、slot wait、PING/reconnect、total、threadCPU、pool_size/free/status/TID/steady started_us，无用户payload/密码/key/Token。原初始化、Acquire等待期限、健康PING、重连失败slot归还、shutdown/Release路径全部保留。trace对象在pool lock之前构造，析构日志位于lock析构之后；任何日志异常不能逃入租借业务。

Gateway原已有100ms阈值/8条每秒慢样本，仅在此诊断开关ON补TID和绝对started/permission/route/persist/unread区间，复用原Measure的start/end时钟，未改取消、ACK、deadline或慢样本阈值。重复phase区间为首start到末end并集，仅诊断关联不能当独占CPU。

计划：先隔离编译并以真实Redis原248个语义检查分别OFF/ON（共496），保留所有bytes/TTL/wrongtype/int64/order/并发/租借归还/不可用及自有shutdown；借用同原宏/已接受Unread API库，不覆写库或原ELF。再构建单候选Gateway，验证加载及原接口交互，最后按原20k mix采样。诊断会有观察开销，不能作为性能接受版本；有完整a2bb原两Gateway image/Env/配置恢复，其他17不重建。所有源/helper/object/失败保留，逐次Git记录。

## 已完成结果与瓶颈边界

Git源码诊断ad2d94d；隔离构建首轮在NonownedPrefix守卫拒绝，编译/链接成功但0夹具检查，原始失败保留。修正仅统一新的自有namespace与守卫，未删除守卫。attempt2同ELF OFF/ON、pool1/4各124，合496原真实Redis语义检查PASS；原健康PING、TTL、wrongtype、int64、顺序、并发、租借归还及shutdown不削弱。诊断候选image47389a96、ELF4f5308bb，仅本次受控运行，不作为性能收益。

运行stage redis-acquire-mixed20k-control-20261006，子点redis-acquire-mixed20k-20261006，原20000用户/总login100/s、private135/s×60s=8100、list20/s×60s=1200及49公开交互/36断言。8100正ACK/真实投递/原SQL身份确认、1200完整页和操作链正确；私聊ACK P99上界149.4ms、会话页P99 107.684769ms，仍FAIL。原201.0/155.029ms和本轮不是同ELF ABBA，不能将差值称新优化收益。原deadline/ACK/durability保持，所有失败保存。

只读分析stage redis-acquire-mixed20k-analysis-20261006。935条稳态pool预采样均status0，free_slots_before=8有677条，=0只有1条；mutex等待均0.069287ms、slot等待均0.056070ms，通常等待slot较小，个别slot最大20.905ms仍保留。健康PING均1.983801ms，占租借total均2.129828ms的主要部分；threadCPU均0.216761ms。935条是最多8条/秒/进程预选择抽样，不是全部请求总体，样本分位不能当生产租借P99。PING wall包含Redis处理、网络、调度等，CPU低不能单独断言磁盘/网络/宿主CPU为唯一根因。reconnect0样本不等于已测完整故障。

同Gateway容器+TID+绝对时间包含无歧义关联8条：route4条、unread4条，其他927条未归因，不能把未归因全算presence。route样本Acquire均7.228ms、PING均7.1395ms；unread样本Acquire均15.63525ms、PING均15.5505ms、PING最大30.805ms、threadCPU均0.2025ms；相应phase均21.947ms。是慢阈值日志与pool抽样的偏置交集，不得外推全体平均/P99或把phase、Acquire、PING相加。

同MID10条Gateway/仓库/客户端交集：send→ACK均139.097814ms，beforeRepository43.005990ms、repository67.6601ms、afterRepository28.431724ms；repo内commit均47.282ms，最大104.047ms。GatewaypersistRPC均85.5213ms、RPC-minus-repo17.8612ms。嵌套阶段不能相加，10条慢偏置样本不是总体P99。原完整8100 ledger ACK均52.653235ms、P99 149.322436ms。

结论：本轮不支持盲扩Redis连接池；共享网络/调度往返及SQL durable提交仍为待优化具体位置，不能用清内存解决。guest minimum MemAvailable8505188KiB，约8.11GiB。下一处候选须减少可证明的必要往返/唤醒并保留所有权、TTL和取消语义，再通过真实Redis及同ELF对照；文件/群写、好友、AI分别继续追踪，不把私聊结果当全部功能验收。

恢复：19健康；其他17 IDs/images/start、全私有配置、全部原Env/Cmd/HostConfig/mount及SQL持久参数保持。GatewayA恢复CID27a29e26892236d96338261efb56b1a7124538a55d37d189f82001bd96d30aab，GatewayB e9cc648a08ecb2fd47837dfae06db8c63ed6b1a4e04726ee4df3319137965bd1；image a2bb65215bfc85246b946a8c0850e134cd3144d078e5b56c376676c2cd8d1cfa、ELF ca01b00011b29eff29fe776fe3ac2dbaa0b4bb6e7a4ddce76412b82e583579ce、conversation flag1，诊断flag移除。权威restore-summary在control stage，原accepted summary是历史记录。恢复override留存runtime-private，任何再次运行需按最新身份核对。
