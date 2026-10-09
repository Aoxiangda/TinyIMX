# 登录路径诊断结果与剩余性能瓶颈（2026-10-06）

本次结论：登录波动仍未解决；在这次受控诊断中恢复到完整10k登录/P99上界68.4ms，没有重现此前约2.4秒尾延迟。私聊此前150/s负载的两个延迟门槛仍未通过，全部功能及10k–50k极致性能未验收。此次是定位能力的迭代，不能记作性能优化收益或简历容量成绩。

## 变更、验证和原环境保护

诊断源码提交8d6eb0a4618f4b68af5109f194b78c0ea4fd7179。默认OFF LoginRequestPhaseTrace，开启时匹配UID/seq和单调时间；客户端只记录UID%16、复用原时间戳，不改认证/KDF/期限/速率/心跳/中止/统计逻辑。原3秒认证期限、100users/s登录、owned700001–710000固定账号范围保持。

独立构建16项语义回归全部PASS，原eager宏与冻结库经过哈希核对；新Gateway镜像d8e2e1ab5e8209cebcb80cab96d76ed31c246ba6d21aaa2d884561b6de1ce78d、Gateway ELF e222074c3f3279ac2dfaf5593a042a1839c7cc7c15975e1edd9302258dadbf95、own worker09c638f7e5b1b8fbe543f772e04df7dd7ac9e7c6f1750140aee82c889dd71343。原worker ELF没有覆盖；own coordinator唯一改动是binary绝对路径，原协调器1ef1c527335a998809296d487378776715ab0fe8c73ccda90d8fa4392431d8ba保持。UID1000/零CAP/no-net/只读loader与预期missingconfig退出检查通过。

临时开启两个Gateway及User数值诊断，User复用已封存1ff078...镜像；部署前核对无外部9000活跃连接，保存私有inspect/完整原日志/原env/镜像/二进制/HostConfig/mount/cmd和配置SHA。其余16服务IDs/images/start始终保持，Message仍be8b5d...且safe batch1。任何结果finally恢复两个Gateway原a8b7...和User原38dca...，逐项原env/cmd/HostConfig/mount/binary以及healthy验证通过。MySQL durable1/1/1/0/0保持。游戏/其他主机应用保持，没有删除、prune、全局缓存/内存清理、安装SDK或更改Hyper-V/安全设置。

## 完整请求统计与同请求分解

容量场景loginpath10k1：hold30s，10,000次登录全部成功，66,160次心跳全部确认，异常断连0，worker完成与hold门槛PASS。登录均值30.2616728ms，P50上界28.0、P95上界49.8、P99上界68.4、max132.849ms。原histogram ceil100usbin直接作为上界，未再加0.1ms。消息计划和发送均0；这个PASS不包含私聊/群聊/文件/TLS/AI/MCP/故障性能。

625条Gateway记录全部与客户端sent/ACK按UID+seq唯一匹配，未匹配0/时间顺序无效0。以下是同625条成功有界采样的均值，不是总体P99，分位数不能彼此相加：

|同请求阶段|均值ms|
|---|---:|
|客户端起点→Gateway提交前时间点|2.898080|
|Gateway提交前→work开始|0.519806|
|work开始→认证RPC开始|0.117362|
|认证RPC|22.317411|
|认证结束→在线状态开始|0.079366|
|在线状态操作|1.903810|
|在线状态结束→未读开始|0.065987|
|未读查询|1.527848|
|未读结束→响应构造开始|0.037374|
|响应构造开始→客户端ACK|1.518814|
|同625条客户端总计|30.985859|

全部625条的最大加和阶段都是认证RPC。Gateway提交→work样本P99 3.208ms、max6.983ms，在这一批请求中没有秒级Gateway排队。received_at是Gateway解析username后的提交时间；前段包含客户端编码/排队、nginx/socket/reactor及dispatcher开销。response_start取于JSON/SendPacket前，后段包括响应组装/排队/运输及客户端读取，不是纯网络。finished包含后续回放任务提交与日志，不能当ACK到达时间。

402条再按UID与时间包含匹配同一次User handler，且仓储按同UID/TID/时间包含均唯一匹配。该同402条认证RPC均值22.629647ms = handler前1.816923 + handler内19.396888 + handler后1.415836ms。handler前后包含stub/transport/admission/prologue/返回与客户端completion，不能分别称纯排队或纯网络。

同402条密码校验wall16.949604ms、threadCPU12.926455ms；lookup1.972915/0.508542ms；repository19.106430/13.541928ms。密码CPU缺失与负差值均保留原数据：4条wall-CPU差值小于0，不进入非负差分汇总（原因未唯一归因，两时钟计量边界不同可能影响差值），有效398条差值均值4.063613ms。threadCPU与wall差不是唯一scheduler/I/O归因。现有SHA256 PBKDF2 100000迭代保持，不通过弱化密码校验制造收益。

## 资源事实和前次回退的限制

32个每5秒资源样本；完整位于登录结束前20区间，guest加权busy78.456997%，iowait0.441576%，可用内存最低8237324KiB。User加权1.537194核、两个GW各0.449239/0.452182核，三目标CFS throttled_usec变化0。自己的native worker按实际PID/startticks跨19个有效区间加权0.121536核，coordinator20区间0.001381核。不能将全guest未归属CPU都分配给负载发生器，采样最大开销170.149392ms也留档。

此前authcurrent10kdiag在相同原认证速率/期限下真实FAIL，完成9122登录后auth_timeout，完整成功样本P99上界2407ms、guest91.6%、密码wall45.454/CPU15.163ms。此次新诊断成功只是另一时间窗口、服务重建和诊断worker/镜像的结果，**没有因果A/B隔离**；不能将2407→68.4写成代码优化35倍，不能说已修复回退、重启治本或CPU唯一根因。前次秒级慢请求没有完整跨进程同请求时间线，仍需在再次发生时捕捉同样边界，而非不同人群P99相减。

恢复后追加10.079s只读idle全进程差量：8vCPU，guestbusy7.394%，419个进程，最高java0.247核/dockerd0.054/containerd0.038，两GW各0.018核。无压测时并非一直占满；只包含两次采样均存活的进程，会漏短命health子进程，不能外推失败ramp的进程组成。未停止任何进程。sched_schedstats=0未更改，不把schedstat wait0作无排队证明。

## 私聊为何仍未达到目标

已有同MID的40条Gateway/repository慢采样交集：senderACK均值152.636344ms；权限17.03325、路由8.941075、persistRPC87.349325、未读10.528375；persist内repository59.627375，其中commit34.307525，RPC仓储外27.72195ms。这些均值来自同40条有偏慢样本，不能冒充全体P99或唯一全局根因；commit墙时包含server/network/调度/存储而非已证明纯fsync。等待分散在多跳及存储，组件微优化不会自动消除整个尾部。

源码再次确认：MySQL pool网络PING前已unlock；UserRepository lookup lease在PBKDF2前释放；RPC stub锁不跨真实调用；权限已一条双向SQL；未读原子快照已存在。不能重复把这些当未修复的问题。已有safe BEGIN+INSERT+identityread批处理，在同ELF开关10k/150msg/s/60s ABBA把私聊P99 156.7–164.5ms降到132.3–135.0ms，两个ON均好于两个OFF，但所有四轮仍FAIL。已取得局部真实收益，整体尾部未突破。

下一轮修改必须针对持久化RPC的入口/处理/返回与SQL提交共同等待，用同MID/同窗口、原语义下验证减少往返或减少排队，随后以同ELF或明确冻结依赖的A/B验证；新失败必须原样保留。之前callback/CQ/单纯加worker/poller改动有负收益，官方异步建议不能当本项目收益保证。正常密码认证、持久化消息、文件传输和Ollama推理应分别声明完成边界、RPS和尾延迟目标；100ms是现有部分消息门槛，不能替代“全部功能极致”验收。10k–50k在线人数必须同时附真实业务混合/消息速率，不写成50k每秒全功能请求。

## 证据目录

- .local/codex/login-path-diagnostic-source-20261006
- .local/codex/login-path-diagnostic-build-20261006
- .local/codex/login-path-diagnostic-run-20261006
- .local/codex/capacity-loginpath10k1
- .local/codex/login-path-diagnostic-analysis-20261006
- .local/codex/login-path-idle-process-cpu-20261006

旧FAIL、私有原日志/完整inspect、所有own ELF/镜像保留；可公开导出仅数值/审计/回归记录，排除私有env/config/log/ELF并执行内存秘密值扫描。源脚本/结果文档进入Git；生成证据仍以独立SHA归档，不改旧结果。
