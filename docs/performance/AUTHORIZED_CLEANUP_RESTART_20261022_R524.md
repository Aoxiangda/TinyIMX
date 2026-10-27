# 本轮授权清理与重启结果

完成时间（UTC）：2026-10-22T02:16:12.710473+00:00。仅进行维护，未开始新的性能压测。

| 项目 | 核验结果 |
|---|---|
| 临时文件回收 | 10个已结束且未挂载输出，72.40 MiB；E盘实际解压、哈希、源复核均通过 |
| 虚拟机 | 同一Ubuntu，8 vCPU / 16 GiB，<LAB_VM>，正常关机后重启，启动标识已变化 |
| 容器与服务 | 全2530容器保留；原20个运行服务恢复并通过健康核验 |
| 数据与程序 | 九个指定数据库和旧binlog保留；全部保留状态比较相同；1129项程序/存储文件重新哈希相同 |
| 宿主可用内存 | 5.461 GiB → 重启后连续样本 7.126–7.158 GiB |
| 正式负载前内存门槛 | 连续10秒至少8 GiB：尚未通过 |
| 虚拟机可用磁盘 | 21.304 GiB，较既定门槛多102.8 MiB，仍须在新负载前重新采样 |
| 工作窗口与辅助服务 | 工作窗口保留；三项辅助服务原Running/Auto与路径核验一致 |

本轮问题ID：PERF-521（清单）、PERF-522（冷存档回收）、PERF-523（重启与数据核验）、PERF-524（资源与保存）。每项已登记动作、结果文件、比较和下一项验证。

冷存档：`<LOCAL_EVIDENCE_ROOT>\ended-client-artifacts-r522-r1\ended-raw-and-test-tools.tar.gz`。

SHA-256：`0ec25a2f85cf444a3a241dda13bed1821537569eeb7f0d0c665125ef3e091035`。

恢复临时压测输出时需使用同目录的 `independent-restore` 及 `plan.json`；原始Windows结果文件仍保留。

下一项验证：Refresh workload readiness before any formal full-IM load; retain host>=8GiB for10s preload, >=6GiB during load, guest swap-in/out0 and disk floor. Resolve the separately recorded Broker disk-cleanup state before a healthy-state performance comparison.
