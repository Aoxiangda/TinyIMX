# 压测暂停与 Qt6 客户端工作交接

2026-10-07 用户明确要求暂停压测，使用本机 Qt6 设计客户端。本次工作切换后不再启动压测、部署候选或调整运行参数。

暂停前的最新 Git 为 `02c4ad77223ba605ba9db579027470d14b76a105`。当前服务为原接受的两个 Gateway 镜像 `5c2645…`、Social `38dca4…`、Message `be8b5d…`。
`permission20k-runtime-control-20261007-attempt2/restore-summary.json` 已确认原三个镜像、完整环境、配置挂载、健康检查、资源参数与 ELF 恢复，其余 16 个实例保持原身份。切换任务时 19 个实例正常且没有容量工作进程。

所有原始压测、失败、构建、源码审计与恢复记录仍保留在 `.local/codex/`，未做删除、缓存清理、数据库或本机应用调整。

| 已完成测量 | 负载与正确性 | 延迟结论 |
|---|---|---|
| 当前接受版本 20k baseline，attempt4 | 20k 认证 TCP，135 私聊/s × 60 秒 = 8100；完整 50 项列表 20/s × 60 秒 = 1200；49 操作 / 36 断言通过 | 私聊 ACK P99 上界 132.9 ms；精确 ledger P99 132.875910 ms；列表 P99 90.746805 ms。私聊门槛失败 |
| 权限分段诊断 20k，已完整恢复原版 | 同负载、8100 私聊与 1200 列表、功能链正确；低频诊断 flag ON | ACK P99 上界 318.9 ms；精确 ledger P99 318.815278 ms；列表 P99 187.705973 ms。性能失败，不能视作优化版本 |

最新分析在 `permission20k-diagnostic-analysis-20261007/summary.json`。455 个请求以相同 from/to/request_id hash/caller hash 关联 client-handler-repository，零歧义，但为各侧每秒前 8 个请求的有偏交集，不能据此宣称全体阶段 P99。
这些关联样本中，permission client 总耗时均值 16.508 ms，handler 6.279 ms，repository 5.573 ms（Acquire 2.820 ms、Query 2.672 ms）；RPC handler 外均值 9.736 ms。
client 监控/span 收尾均值 0.113 ms，handler 收尾均值 0.049 ms，因此当前证据不支持将观测全局 mutex 认定为首要瓶颈而直接重写。
同一关联子样本的端到端 ACK 均值 128.514 ms；权限阶段只覆盖其一部分，后续应分析消息持久化、排队及客户端/传输边界，不能把剩余全部归于 SQL 或网络。
诊断轮完整稳态区间 guest busy 89.326%、CPU PSI some 72.660%、iowait 0.443%，最低可用内存 8221828 KiB，swap out 0。存在显著调度竞争的证据，没有内存耗尽或磁盘等待主导的证据；不据此更改 VMware 或关闭其他应用。
baseline 与 traceON 不构成同 ELF 的 OFF/ON/ON/OFF 性能对照，因此不能把退化百分比全部归因于诊断代码，也不扣除诊断成本修正失败指标。

此前默认 OFF 的 private insert-first 候选在 560 项隔离 SQL 验证中正确，但 created 没有稳定耗时收益，重复请求平均耗时约 3.70 倍；保留源码及失败结论，未启用或部署。
秒单位直方图 bucket 已修复并通过真实 SDK 23 检查及 Runtime 回归；它提高观测精度，不能描述成端到端性能突破。

首次权限诊断切换因 Compose 缺少状态目录变量，将三个候选的配置挂载错误解析为 `/config`，压测尚未开始。失败控制器及三个恢复尝试均保留。
后续恢复校验了原 Binds/Mounts 表示与完整 HostConfig，实际 Compose 正负 7 项预检通过后才重新切换；修复版诊断完整结束并恢复原版。
`/config` 目录原有归属不能证明，已审计并保留，未删除。

后续恢复压测需由用户重新要求。客户端设计文档和 Qt6 UI 原型不代表 20k–50k 全功能极致性能完成，也不扩大此前 10k 限定混合通过结论。
