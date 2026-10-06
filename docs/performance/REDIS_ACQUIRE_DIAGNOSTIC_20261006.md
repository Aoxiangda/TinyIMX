# Redis租借阶段诊断候选（2026-10-06）

状态：源码候选，真实Redis回归/构建/部署/20k诊断均NOT_RUN，未替换当前已接受a2bb网关。它不是新的性能修复或验收结果。

动机：20k指定mix仅延迟FAIL，同MID19慢样本permission/route/unread和仓库前后有多段等待，原0条pool/CPU样本不足以区分连接slot、健康PING、命令处理或调度。内存8.165GiB可用，不以清缓存或关闭游戏解决。完整证据见MIXED20K_BOTTLENECK_AND_NEXT_20261006.md。

新增TINYIMX_REDIS_ACQUIRE_TRACE_ENABLE严格1才启用；默认OFF不调用新增观察时钟/线程CPU。开启时Acquire仅选至多每125ms一个数值样本，记录mutex wait、slot wait、PING/reconnect、total、threadCPU、pool_size/free/status/TID/steady started_us，无用户payload/密码/key/Token。原初始化、Acquire等待期限、健康PING、重连失败slot归还、shutdown/Release路径全部保留。trace对象在pool lock之前构造，析构日志位于lock析构之后；任何日志异常不能逃入租借业务。

Gateway原已有100ms阈值/8条每秒慢样本，仅在此诊断开关ON补TID和绝对started/permission/route/persist/unread区间，复用原Measure的start/end时钟，未改取消、ACK、deadline或慢样本阈值。重复phase区间为首start到末end并集，仅诊断关联不能当独占CPU。

计划：先隔离编译并以真实Redis原248个语义检查分别OFF/ON（共496），保留所有bytes/TTL/wrongtype/int64/order/并发/租借归还/不可用及自有shutdown；借用同原宏/已接受Unread API库，不覆写库或原ELF。再构建单候选Gateway，验证加载及原接口交互，最后按原20k mix采样。诊断会有观察开销，不能作为性能接受版本；有完整a2bb原两Gateway image/Env/配置恢复，其他17不重建。所有源/helper/object/失败保留，逐次Git记录。
