# 本轮授权维护与首个有效对照点（2026-10-21）

用户授权：内存不足时释放不需要的应用/环境，正常重启虚拟机。后续明确要求保留所有工作窗口；该指令之后没有关闭工作窗口。没有修改简历或Canva。

已完成正常维护，原8vCPU/16GiB、双网卡和磁盘配置保持。初始原2292容器及新增11个结束测试容器共2303均保留，原20服务已恢复健康。原业务库2267400消息/1698446 outbox、九个指定库各30000/30000、binlog及1/1/FULL设置保持。最后重启前后519项原生/冻结文件（含本轮完整存储）与84项SDK文件完整校验相同。

首个冻结R212 OFF1通过完整资格：

| 项目 | 实测 |
|---|---:|
| 认证连接 | 10000 |
| 发压 | 700条/秒，60秒，128B，同网关配对 |
| 完整成功 | 42000 / 42000 |
| ACK均值 / P99 | 5.598952 / 18.084400 ms |
| 发送计划→投递P99 | 18.374729 ms |
| 整台VM平均忙核 | 5.220311 |
| 保守统计窗口 | 59.119732秒 |
| 测量期Windows最低可用 | 6.528667GiB |
| guest swap in/out | 0 / 0 |
| native pending events / pending deliveries / quarantine | 0 / 0 / 0 |
| 本轮隔离库物理外键 / 孤立引用 | 0 / 0 |
| Broker事件 / Redis投影 | 42000 / 20000，全匹配 |

这一轮是优化关闭的基线。尚无完整ABBA、容量/长期稳定性或新的配对WuKongIM结论，不能将其与旧环境R24/R30之差归因于代码优化。代码默认不变。

实际页统计：pages=94740、decoded=1027384、unvisited=954325、enqueued=42000。下一变量为按需读取demand=0→1；同九程序、pending index=1及其他开关保持。该计数是累计工作量，不能当作独立消息数或直接折算CPU收益。

Windows客户端每条请求同一QPC计时：计划→入队均值0.350277ms，入队→write调用均值0.020208ms，write调用→ACK均值5.578744ms。最后一段包含socket写入、完整服务器与回程，不能声称已拆出纯RPC或事务提交。1045条早唤醒时戳保持原值。系统页读入60桶描述性分析已留存，尚未识别是哪个进程/文件引起，相关关系不能替代因果验证。

最新资源资格：主机连续采样7.730988–7.836880GiB，低于8GiB/10秒门槛；最近磁盘读数比原15%+650MiB预留线高31154791字节，guest swap0。ON1/r157四张表仍0、cache停止、case目录尚未建立。没有启动第二臂，没有合成或补写测量结果。

安全迁出：源文件合计414024701字节，三步实际free差额合计380497920字节，两个口径分开记录。所有移出的分析和日志字节都已在本机E实际解包并校验；当前日志保留，未删除程序、SDK缓存、任何SQL数据库、binlog或native存储。旧guest分析读取器需按归档指针恢复原member。

重启更换了Proxy StartedAt，ID/image/配置及最新日志enableBatchAck=false保持。旧R260启动时期守卫对当前实际状态拒绝，测试了真实断言而未启动控制器。先完成四个R212资格，再另封当前受控启动时期SDK代次；旧profile不覆盖、守卫不绕过。

每项问题的动作、结果、比较与下一验证（完整清单在ISSUE_LEDGER_20261017.json）：

| ID | 状态 / 下一验证 | 主证据 |
|---|---|---|
| PERF-271 | SAME8CPU16G_BOOT_CHANGED_2292_RETAINED20_HEALTHY_DATA_RUNTIME_HASHES_EQUAL；保留工作窗口；后续仅在独立8GiB/10秒、磁盘和swap资格同时通过后进入ON1，不反复无条件重启。 | benchmark/results/peer-20261017/PERF271-authorized-memory-maintenance-r1/result.json |
| PERF-272 | IDLE_AUXILIARIES_RELEASED_NETWORK_CORE_DISPLAY_DRIVER_PRESERVED；当前工作窗口保持；如恢复NVIDIA App功能可正常启动该工具服务，网络核心和显卡驱动不需恢复。 | benchmark/results/peer-20261017/PERF272-idle-overlay-cleanup-r1/result.json |
| PERF-273 | ONE96MIB_ARCHIVE_ACTUAL_E_RESTORE_BEFORE_REMOVAL；需恢复旧系统日志时，从E原member恢复并遵循记录的uid/gid/mode。 | benchmark/results/peer-20261017/PERF273-archived-system-journal-offload-r1/result.json |
| PERF-274 | GREETER_ONLY_STOPPED_DEFAULT_GRAPHICAL_TARGET_PRESERVED；后续各正式臂保持greeter inactive；需图形界面时正常启动display-manager，任何环境变动须在新对照中一致记录。 | benchmark/results/peer-20261017/PERF274-idle-guest-greeter-stop-r3/result.json |
| PERF-275 | OFF1_RESUMED_ON1_CURRENT_HOST_GATE_NOT_MET；ON1仍用r157、相同九程序、仅demand开关0→1；先独立复核8GiB连续10秒，测量期间仍须6GiB，不能降低门槛或把预备当实测。 | benchmark/results/peer-20261017/PERF275-post-maintenance-readiness-r1/result.json |
| PERF-276 | AUTHORIZED_RESTART_CHANGED_PROXY_EPOCH_OLD_SDK_GUARD_CORRECTLY_REJECTS_REUSE；先完成原四臂R212资格，再另行冻结适用当前受控启动时期的SDK profile/controller代次；保留旧R260，不绕过或默默放松旧守卫。 | benchmark/results/peer-20261017/PERF276-post-restart-sdk-proxy-epoch-r1/result.json |
| PERF-277 | DESCRIPTIVE60_BUCKET_ANALYSIS_SAVED_NO_CAUSAL_CLAIM；分离诊断臂追踪各进程硬缺页/文件支持读入；TinyIMX与WuKongIM保持一致冷热条件；不把侵入式诊断放进正式零诊断臂。 | benchmark/results/peer-20261017/PERF277-off1-host-page-input-analysis-r1/result.json |
| PERF-278 | TEN_FILES_E_ACTUAL_RESTORE_ANALYTICAL_COPIES_AND_CLOSED_LOGS_OFFLOADED；原规范结果和Windows端完整副本保留；重跑旧guest观察器前从E恢复指定member；后续每轮仍需新磁盘资格。 | benchmark/results/peer-20261017/PERF278-ended-evidence-offload-r2/result.json |
| PERF-279 | NATIVE_ROTATION_CONTENT_PRESERVED_E_RESTORED_NO_ACTIVE_TRUNCATION；仅在新资源需求下安全轮转，不为制造指标反复轮转；严格区分源字节与观察free。 | benchmark/results/peer-20261017/PERF279-system-journal-rotation-r1/result.json |
| PERF-280 | POST_OFF1_INSUFFICIENT_HOST_MEMORY_RESTARTED_WITHOUT_WINDOW_CLOSE；尊重保留所有工作窗口；仅满足原资格后开始ON1。 | benchmark/results/peer-20261017/PERF280-post-off1-resource-readiness-r1/result.json |
| PERF-281 | REPORTING_SCOPES_SEPARATED_PRIOR_RESULTS_UNALTERED；下次出现差额时在移出前取得块分配和指定inode只读打开句柄证据，再与句柄自然关闭后的free比较，不能凭字节差推断原因。 | benchmark/results/peer-20261017/PERF281-offload-byte-scope-audit-r1/result.json |
| PERF-282 | WINDOWLESS_SEARCH_CACHE_ONLY_WORK_WINDOWS_PRESERVED_HOST_STILL_BELOW8；不再重复结束同一自动启动组件来试数；工作窗口保持，ON1等待独立原门槛通过。 | benchmark/results/peer-20261017/PERF282-idle-search-cache-release-r1/actions.json |

资源维护没有完成整个性能追平目标。下一臂只在独立原资格通过后执行，工作窗口保持；完整四臂、独立SDK矩阵、新环境WuKongIM对照和长稳/容量验证仍未完成。

归档位置：<LOCAL_EVIDENCE_ROOT>/maintenance-and-first-formal-r284。封存结果及完整恢复校验以benchmark/results/peer-20261017/PERF284-maintenance-E-seal-r1/result.json和该目录manifest.json为准。

最新归档位置：<LOCAL_EVIDENCE_ROOT>/maintenance-and-first-formal-r284-r2。封存结果及完整恢复校验以benchmark/results/peer-20261017/PERF284-maintenance-E-seal-r2/result.json和该E目录manifest.json为准。r1包写入后因Windows只读句柄fsync失败而保留失败范围，不能视作已验证；当前工具改用可写句柄刷新。
