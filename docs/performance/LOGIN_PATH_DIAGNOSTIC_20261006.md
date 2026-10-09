# 同请求登录路径诊断（2026-10-06）

状态：16项回归与独立构建已PASS；单次10k登录保持诊断完成并恢复原环境，登录P99上界68.4ms，625客户端/Gateway与402 User仓储同请求关联。它是关联证据，不是性能收益或全部功能验收。详见[实际结果与未解决项](LOGIN_PATH_DIAGNOSTIC_RESULT_20261006.md)。

前一轮62761db确认同Gateway/Message/User原镜像、发生器和100/s登录，从10000完整/P99约75–80ms退步到6357中止/P99约2424ms。379成功认证样本密码CPU15.16ms/wall45.45ms，guest busy91.6%；等待尚未唯一归因。继续猜SQL/Logger/某个锁并重跑容量不能填补该缺口。

新增LoginRequestPhaseTrace默认OFF，关闭时不读取诊断时钟/CPU或输出。开启时保存received、work start、Authenticate/SetOnline/GetTotalUnread各自start/end/threadCPU、response构造前和work结束绝对monotonic时间，成功UID%16、失败也eligible，Gateway每second最多8行。原请求deadline/cancel/session fences/调用顺序、结果所有权和异常保持，不记录密码、username、body或key。response_us是生成/排队之前，不代表实际发送；finished包含提交登录后回放、日志和诊断析构，不能当作ACK送达时间。

客户端只在exact TINYIMX_LOGIN_CLIENT_TRACE_ENABLE=1保存UID%16的login_sent/login_ack七字段台账行，时间复用原login_start/packet now，无额外clock；空cid不参与原私聊reconciliation。所测发送起点包含客户端output队列；没有改登录/心跳/消息节奏、deadline、histogram或失败中止规则。原冻结worker不覆盖，后续需own新ELF与own coordinator copy明确审计。

最小构建复用通过130checks的冻结eager archive+main、控制宏为30452旧eager，只重编GatewayServer并替换自己的archive成员。16语义测试包含OFF zero clock/sink、move-only、绝对时序、CPU missing、异常和原期限；新own worker编译四个实际源码，无SDK安装/CMakecache重建或原ELF覆盖。仅封装独立base a8b7镜像、UID1000零CAP/no-net/readonly loader验证，正式19容器/私有配置保持。

后续若暂时部署，仅在确认无外部客户端后保存原inspect/env/config/logs，加入默认OFF诊断开关，使用一次原100/s10k hold而非反复性能矩阵；记录实际客户端CPU及per-CPU/guest PSI，任意结果都恢复原服务。Gateway数值与User既有数值按同UID/TID/时间包含和client台账匹配，检查每条时间顺序。成功有界采样不是总体P99；RPC之外差值包含入口/transport/调度/方法prologue/日志返回，不能称pure network。sched_schedstats当前0，未开启系统统计，不把0等待当证据。

官方gRPC性能指南建议性能敏感场景评估异步API：https://grpc.io/docs/guides/performance/ 。本项目以前callback/CQ机制对照已有负收益记录，不能按指南盲目替换而宣布根因。当前先用真实阶段证据决定修改。全部功能10k–50k目标仍未达到。
