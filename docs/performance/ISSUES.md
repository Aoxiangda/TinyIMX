# TinyIMX 本机容量与可靠性问题台账

本台账随 Git 迭代更新。目标覆盖 10k、20k、30k、50k 在线用户下现有全部功能，以及状态衔接、混合流量、热点、背压、故障恢复和长时间运行。每一档分别验收，不将小型功能测试或旧版本成绩替代新候选的规模结果。

记录原则：观察到的事实、根因推断、源码修改、验证结果分别记录；未执行和失败的项目保持原状态。业务指标采用活动窗口与 drain 分开的统计口径，证据绑定源码提交、实际二进制、镜像、配置身份和环境。

## 迭代记录

| 迭代 | 目的 | Git / 状态 |
|---|---|---|
| 接管基线 | 保存已有 M21、R1、R2A 和压测工具改动；排除配置凭据、历史备份及生成文件 | `90581ea`，分支 `work/local-capacity-10k-50k-20261004`；Git preflight 通过 |
| R2B1 本机候选 | 根据原对话合同接通只读 `ResolvePrivateMessage`，进行本机编译和回归 | 领域、协议、真实 gRPC 回环、R1/R2A/Gateway 回归及真实 MySQL 只读回环通过；原交付 ZIP 未在本机找到，本候选独立记录 |

## 本次接管中遇到的问题

### ENV-001：桌面控制运行时无法启动

- 现象：Computer Use JavaScript 运行时初始化和重试都返回 `windows sandbox failed: helper_unknown_error: apply deny-read ACLs`。
- 原因范围：本机自动化运行时的沙箱初始化失败；不据此判断 Ubuntu 或业务系统故障。
- 处理：通过 VMware 自带 `vmrun` 查询虚拟机和验证 guest 登录；从已认证的 guest 读取主机公钥，再建立严格核验的 SSH 连接。
- 验证：返回用户 `jackson7`、主机 `ubuntu`，且主仓目录存在。已直接执行 Git、资源、源码与容器只读核查。
- 状态：命令行接管已验证；桌面运行时本身仍未修复。

### ENV-002：SSH 可达但没有已核验的主机密钥和登录身份

- 现象：22 端口开放，但初次严格连接返回 `Host key verification failed`。
- 处理：使用用户授权的 guest 登录安装本次专用公钥；经 VMware 读取 Ubuntu 主机公钥并固定到本地连接配置。私钥和连接配置存放在项目仓外。
- 验证：严格主机密钥检查与专用公钥登录成功；后续自动化连接不需要凭据进入项目源码或证据。
- 状态：已解决。

### ENV-003：PowerShell 管道给 Bash 输入附加 CR 字符

- 现象：第一次只读核查完整输出后出现 `/bin/bash: line 59: $'\\r': command not found`，退出状态非零。
- 原因：跨平台标准输入的行尾转换。该错误发生在脚本结尾，不能解释为 Docker、数据库或 SSH 登录故障。
- 修复：本地连接入口统一 LF，再在远端输入层移除 CR。
- 验证：后续交接核查和 Git 基线保存脚本均返回 0。
- 状态：已解决。

### GIT-001：主仓有大量未提交的已有改动

- 现象：原分支 `feature/m21-production-release-v1`，HEAD 为 `bb3e3a2`；62 个已跟踪文件改动、96 个未跟踪条目，涵盖源码、压测脚本和历史备份。
- 风险：后续修复无法与已有版本区分；运行成绩无法仅凭原 HEAD 追溯。
- 处理：在 `.local/codex/` 保存原 diff、状态和已有改动文件归档；创建工作分支，将已有源码和工具保存为明确标注的接管基线；生成文件及历史备份添加忽略规则。
- 验证：基线 `90581ea`；仓库自带 5 项 Git preflight 全部通过。`deploy/production/.env.example` 保持未提交，后续不使用 `git add .`。
- 边界：基线提交保存已有内容，本身不是新增性能修复，也不证明这些内容已经统一部署。
- 状态：已建立可追溯基线。

### REL-001：R2B1 交付包未落到本机，接口未实现

- 现象：Linux 和 Windows 下载目录均未找到 `TinyIMX_R2B1_Resolve_20261003.zip`；主仓没有 `ResolvePrivateMessage` 定义。
- 已有依据：底层仓库已提供按 `(sender, client_message_id)` 的查询；原对话明确了 R2B1 的三类结果和只读语义。
- 修复方向：接通 Repository Adapter → Application → gRPC 服务 → RPC 客户端。匹配 sender/C/recipient/type/canonical content 后返回原状态；冲突不返回原正文；未观察到不表示永久未提交；存储错误和旧服务不支持接口必须保留为错误。
- 验证结果：领域测试和协议合同测试通过；真实 gRPC 回环验证迟到提交、冲突无正文、存储错误与旧服务不支持接口保持错误；客户端拒绝 11 类畸形响应。R2A 22 项、R1 15 项、Gateway 38 项回归通过，`message_service_demo` 和 `gateway_demo` 编译链接成功。
- 真实 MySQL 结果：在临时回环服务中，398858、398888 保持 Pending；421466、421467 保持 ReceiverConfirmed。四条历史消息共 20 项检查通过，核对匹配、冲突、未观察到及原记录不变。未调用 SQL mutation，未启动压力，也没有执行历史恢复。
- 状态：只读接口的本机构建和上述验证通过。Gateway 自动核实、持续投递恢复和生产候选部署尚未完成，原 10k FAIL 保留。

### REL-002：源码修复与运行制品尚未绑定

- 现象：主仓已有 R1/R2A；当前 19 个容器仍运行既有 `tinyimx/runtime:m21-final` 或基础服务镜像，运行时间约两天。
- 含义：不能仅因源码存在就将容器行为视为新修复行为；镜像标签自身也不足以证明二进制身份。
- 处理方向：构建验证后记录二进制 SHA、镜像 ID、容器身份与配置摘要，并在候选部署和压测前核对一致性。
- 状态：未关闭；尚未在本轮部署候选。

### PERF-001：原 10k 正常私聊负载失败

- 证据来源：原“项目落地压测”对话和其历史证据，当前接管尚未独立重跑。
- 原结果：30,000 条发送、27,500 条成功 ACK、2,500 条负 ACK；durable 27,706、confirmed 27,498、Pending 208；成功 ACK P99 约 8.266 秒。
- 已确定的修复线：调用前预算耗尽；持久化接受 ACK 等待远端投递；不确定持久化与 peer 尝试结束后的持续恢复缺口。
- 当前进度：R2A 的小型真实验证在原对话中通过；R2B1 只读依据正在补齐；完整自动核实、持续投递和 R2C 热点定位尚未完成。
- 边界：原 P1-B 的 73 条 Pending 与本次 208 条来自不同实验；不合并、不人工改状态。两个消息的功能验证不替代 10k P99。
- 状态：历史 FAIL 保留。

### ENV-004：需要区分可回收缓存、历史 swap 和实际内存压力

- 观察：Ubuntu 总内存约 15,946 MiB、可用约 10,533 MiB；swap 中有约 1,872 MiB 历史页；短时 memory PSI 为 0，后两次 vmstat 的 si/so 均为 0。Windows 接管检查时可用约 2.81 GiB。
- 判断：这次快照没有显示 guest 内存不足；有历史 swap 不等于采样时正在换页。宿主余量仍应在压力期间记录。
- 处理：本轮未清理缓存、未关闭用户应用、未停止业务依赖。升压前和升压期间同步检查 host/guest 工作集、PSI、换页和容器占用。
- 后续清理规则：优先释放已确认过期且属于本任务的测试/构建进程和闲置资源；记录清理前后环境，保持对照实验可比性。
- 状态：持续监测；没有将清理内存虚构为性能修复。

## 后续条目模板

### DEV-001：新增回环测试的局部变量与原测试重名

- 触发：本机第一次构建 `message_service_integration_tests`。
- 现象：新增 `conflict_request` 与原 Persist 用例中同名变量处于同一 `main` 作用域，编译器报告 conflicting declaration。
- 范围：测试代码编译问题；领域实现与 RPC 协议测试已构建并执行通过。不是业务压测失败。
- 修复：将新增变量命名为 `resolve_conflict_request`，保留原 Persist 测试。
- 验证：修复后的真实 gRPC 回环目标、后续 R1/R2A/Gateway 回归和两个服务构建全部通过；第一次失败日志保留在 `.local/codex/r2b1-local-20261004/build-message_service_integration_tests.log`，修复后日志为同目录 `.attempt2.log`。
- 状态：已解决。

### TEST-001：已有 File 故障脚本的崩溃点晚于上传完成

- 源码证据：`scripts/tinyimx_capstone_file.sh` 先执行 prepare，并要求 upload → finalize → partial-download 的 PASS，再杀 FileService。
- 含义：该用例能测试已 finalize 对象的下载恢复，但没有覆盖“上传未完成时”崩溃后的同身份续传。
- 处理方向：补充上传中间状态屏障、崩溃、恢复、续传、最终 SHA 和授权边界的实际执行器，并与 IM 混合负载一起验收。
- 状态：覆盖缺口已确认，尚未修复，未新增该场景 PASS。

### TEST-002：当前 LocalCapacity 执行器仅支持 hold/private

### REL-003：Pending 需要跨进程持续发现，而非只在登录时查询

- 实测基线：2026-10-04 的只读查询发现 Pending 46,419 条、接收者 2,367 个。其中 46,210 条创建于 9 月 30 日，208 条创建于 10 月 3 日，另 1 条创建于 10 月 1 日。旧数据不删除、不改状态，不与 10k 实验的 208 条混为同一结果。
- 索引证据：现有 `(to_user_id, delivery_status, message_id)` 索引支持去重覆盖扫描；单次 `EXPLAIN ANALYZE` 返回 257 个 ID 的本次测量约 13.3ms，不代表负载中的 P99。
- 源码原因：R1 记录入队前责任，`MarkStarted` 后释放；重放查询失败不持续恢复。原网关退出后，仍在线接收者缺少持久发现触发。
- 当前候选：新增只读 `ListPendingRecipientsAfter`，最多 256 个去重 ID 加一个 SQL sentinel，不加载正文、不依赖原网关内存、不更改表。领域和 RPC 双层校验游标、排序、数量和 continuation；预算包含端点发现时间；旧服务不支持接口保持错误。
- 审计与回滚：`audit/r2b2-before.json` 保存逐文件 SHA 和参考副本；远端安装要求 Git HEAD、文件前后 SHA、路径边界全部匹配，先归档再替换。
- 验证：提交 `7d10816` 的领域 43、协议 151、真实 gRPC 64、只读 MySQL 25 项全部通过，合计 283 项。MySQL 全周期 10 页发现 2,367 个接收者，本次完整验证约 314ms；新服务实例无需原网关内存即可发现相同首批 ID。证据在 `.local/codex/r2b2-local-20261004/`。自动公平调度、投递窗口、运行容器部署和 10k～50k 验收仍未完成。
- 状态：候选；仅补齐持久扫描基础，尚未关闭完整恢复缺口。

### PERF-002：新私聊投递注册遍历所有跟踪记录

- 源码证据：`ReceiverDeliveryTracker::RegisterAttempt` 为新私聊检查 receiver mismatch 时遍历 `entries_`；`RegisterRetryAttempt` 与未知消息 ACK 也有同类遍历。
- 含义：累计 n 个未确认记录时，逐个新建的检查累计可能达到平方级；当前仅已确认缓存有上限，未确认集合需额外资源窗口。
- 处理方向：保留 M/receiver 与迟到 ACK 语义，以直接索引消除遍历；配合有上限的投递窗口、每接收者公平扫描与持续持久责任。
- 当前候选：新增私聊 M → receiver 的直接索引，与主跟踪表共用互斥锁；群消息不进入该索引，确认缓存淘汰同步移除索引。注册、错接收者检测及 timeout snapshot 不再扫描主表。迟到 ACK、群消息独立接收者与原 retry 规则保留。
- 验证：提交 `7dc734b` 的 Gateway 41 项全部通过。相同 VM、编译器、O2 下 12 次前后对照（每版每规模 3 次）全部通过、checksum 相同。5 万记录的注册中位数 17,646.6ms → 39.0425ms，5,000 次 snapshot 查询 5,605.58ms → 0.651611ms；峰值 RSS 中位数 16,372 → 18,608KiB。证据在 `.local/codex/tracker-index-20261004/`。记录数是数据结构规模，不是在线用户数；尚无新的端到端容量成绩。未确认集合的资源窗口仍需下一阶段实现。
- 状态：已确认源码开销与资源边界缺口，未关闭。

### TEST-002（详情）：当前 LocalCapacity 执行器仅支持 hold/private

### REL-004：持续恢复需要有界、公平且可取消的调度元数据

- 候选：新增共享生命周期的调度器；每次一个 discovery RPC、至多 256 个待派发接收者、每 tick 32 个接收者、至多 65,536 个会话游标和 32 个活动重放。重放每任务一页 100 条，保存扫描进度，终页回到 0；错误与未开始任务不推进游标。
- 责任边界：元数据不是持久事实源；SQL Pending 保留恢复依据。新 epoch 和 Stop/Resume 世代拒绝旧查询/旧游标完成，断连仅退役匹配 epoch。查询 worker 不捕获 Gateway 或 Session，事件线程仅 bookkeeping 和非阻塞 Submit。
- 验证计划：单 flight 并发、拒绝与任务析构、错误页、游标循环和迟到提交、旧会话 fencing、状态容量、退出世代，另编译 Gateway 并保留原回归。
- 启用边界：源码接线暂设 `enable_durable_private_recovery=false`，等待未确认/字节/重试窗口及真实恢复验证后统一启用；没有部署容器，没有新增全功能或规模 PASS。
- 验证：提交 `20ab7a1` 的调度组件 36、Gateway 41、R1 15 项全部通过（合计 92），`gateway_demo` 实际编译链接成功。证据在 `.local/codex/durable-scheduler-20261004/`。
- 状态：组件与默认关闭的接线已验证，真实自动交付仍待验证。

### REL-005：未确认数量、正文保留和尝试序号缺少完整窗口

- 审计候选：跟踪器限制 waiting 总数 200,000、每接收者 64、wire-byte 总额 256MiB、每接收者 1MiB。私聊与群聊共享接收者资源额度但保持业务身份命名空间独立。只有合法真实 ACK 释放等待窗口；重复 ACK 不重复释放，拒绝入队不抹去原证据或 SQL Pending。
- 重试证据：每身份最多保留 64 个 D 序号。持续恢复重发间隔至少 5 秒；达到序号证据上限后重传当前已知 D，不新建 M、不增长序号集合，并保留最早真实迟到 ACK 的效力。原 timeout 主动重试上限仍为 3。
- 启用控制：`gateway_demo` 支持显式环境开关 `TINYIMX_DURABLE_PRIVATE_RECOVERY_ENABLE=0/1`，源码默认仍关闭，供隔离 E2E  opt-in；生产容器尚未变动。discovery request/trace ID 改为每次查询唯一，避免周期回绕造成观测混淆。
- 验证计划：数量/字节额度、溢出、ACK 释放、增量 reservation 拒绝不改变旧尝试、跨域共享额度、并发接纳、重试证据上限/已知 D 重传及最早 ACK；再执行 Gateway、R1、调度组件和实际 Gateway 构建。
- 实际内存快照：04:48 UTC，guest 可用约 7,666MiB、PSI 平均为 0、短时 si/so 为 0；host 可用约 1.33GiB，压力实验前需持续采样。没有清缓存、切 swap、终止用户应用。旧 bundle/image 脚本含删除目录及 swap reset，未直接调用。
- 状态：候选，尚待构建、真实恢复和负载验证；这些窗口值是当前控制参数，不是已证明的容量上限。

- 源码证据：`benchmark/local_capacity/capacity_worker.cpp` 的 mode 校验只有 hold/private；group delivery 的计数名称明确标注不是 group test。
- 含义：它可作为连接和私聊的规模证据基础，不能作为群管理、File、AI/MCP 或全部交叉功能的规模验收工具。
- 处理方向：根据实际 Packet/RPC 全量清单补齐 actor、状态链、混合流量及逐项核对，再分别运行各档在线规模。
- 状态：覆盖缺口已确认，尚未完成执行器适配。

### PERF-003：虚拟机精确时钟读取放大投递注册成本

- 同条件证据：`.local/codex/window-cost-profile-20261004/` 的 12 次交替对照，50k 条注册中位数索引版 34.564ms、资源窗口版 225.791ms；20 万次精确单调时钟读取 757.543ms，而 coarse 单调时钟读取 0.663414ms。窗口版逐条记录时间，新增精确读取能解释主要开销。
- 修复候选：仅对多秒重发冷却引入独立 `DeliveryRecoveryClock` 时间域。Linux 在报告分辨率不超过 10ms 时使用 coarse 单调时钟；不可用时回退单调时钟。冷却比较增加一个报告分辨率间隔，避免在时钟刻度边缘提前重发。
- 边界：RPC 超时、业务 deadline、统计耗时继续使用原精确时钟；不改 host/guest 时钟源，不改变数量/字节/尝试证据限额、真实 ACK 权威和 SQL Pending。
- 验证计划：独立时间域、单调读取、冷却边界、迟到 ACK 和原 Gateway/R1/调度回归；同条件交替基准及真实网关构建。组件优化不等于 50k 在线容量通过。
- 验证：提交 `86b12ff` 的 Gateway 51、恢复组件 36、R1 15 项全部通过（共 102 项），网关编译成功；6 次局部基准通过，50k 注册中位数 37.3257ms、峰值 RSS 中位数 19,452KiB，数量/字节/重试限额保留。原始证据在 `.local/codex/recovery-clock-20261004/`。
- 状态：组件热路径修复已验证；端到端容量仍未验收。

### REL-006：持续恢复必须由真实进程故障和应用 ACK 验证

- 当前执行器：`benchmark/local_capacity/recovery_e2e.py`。两个独立 Gateway、一个独立当前 MessageService、独立 64MiB Redis，只监听 loopback；现有容器和配置不切换。
- 断言：源码网关 SIGKILL 后，接收者不重新登录即恢复；独立 MessageService 停止/重启后保持相同要求。先查询 SQL Pending，接收真实正文后仍为 Pending，只有接收者实际 ACK 后才为 ReceiverConfirmed。
- 范围：已有两名测试账号、两条新正常业务消息；历史 Pending 不修改、不删除。测试流量 gate 只延迟原始 TCP 字节、不伪造响应，不代替实际故障进程退出。
- 审计：运行前记录端口、进程计划、容器 image 与标签、读写范围及回滚方法；结束只停止本轮子进程与带匹配标签的本轮 Redis，保留测试消息和停止的容器。私有配置留在 mode 600 子目录，禁止导出凭据。
- 首次实验：`.local/codex/recovery-e2e-live-20261004-attempt1/` 为 FAIL，登录返回 `auth_unavailable`，尚未生成测试消息。根因是执行器把 UserService 50052 与 SocialService 50051 映射写反；现有服务未故障。失败代码提交 `5ce1f97`，日志和停止的测试 Redis 保留。
- 修正候选：读取已有容器的实际监听命令，严格解析唯一端口并先做连接检查，再创建本轮资源；不再依赖记忆中的静态端口。登录失败记录脱敏原因。
- 状态：前置错误已定位，修正后待新实验；不是 10k～50k 全功能容量成绩。

### TEST-003：真实恢复执行器重用强杀网关的未过期租约

- 第二轮 `.local/codex/recovery-e2e-live-20261004-attempt2/`：来源网关 SIGKILL 后，消息 421468 在接收者没有重新登录的情况下恢复，真实 ACK 后 SQL 收敛，第一场景 PASS；单次冷启动 ACK 164.08ms、恢复约 8006ms，不是 P99 或容量成绩。
- 后续前置 FAIL：第二个来源网关重用同一 registry identity，旧租约尚未过期，`gateway registry lease start failed`。这是独立场景的执行器拓扑问题；不能用删除或抢占租约来掩盖。
- 保存：提交 `e74bf8d` 保留完整第二轮代码和失败摘要；现有 19 个容器身份不变，测试 Redis 停止保留，测试业务消息保留，历史 Pending 未修改。
- 修正候选：为后续独立 MessageService 重启场景使用新网关 identity，保留原租约到自然过期。运行同样真实故障与 ACK 断言。
- 状态：待第三轮真实验证。

### REL-007：100 条大正文 Pending 页可能超过 RPC 接收上限

- 源码证据：Pending 请求最多 100 条，原应用层按数量返回全部正文；当前 MySQL TEXT 单条可接近 64KiB，100 条约 6.5MiB，足以超出普通 gRPC 接收预算，恢复查询失败后游标无法推进。
- 候选：保持请求数量上限和 protobuf 不变，按总计 2MiB 的保守字节预算截断响应，包含正文、client ID、三个时间字符串和每条 256 字节 framing 余量。短页 `has_more=true` 继续读取最后返回的 M 之后，未返回消息仍在 SQL。
- 错误边界：超出请求条数的 repository 返回、单条第一记录无法装入、坏身份或坏排序均报错；不把错误伪装成空终页，不人工确认或删除消息。变量字段采用减法边界检查避免溢出。
- 验证计划：100 条近 TEXT 最大值的真实 gRPC 查询、实际 protobuf 字节数检查、分页全部返回一次及 SQL 责任不变；领域异常页和原领域/gRPC 回归。隔离比较原应用源码，应保留旧实现的失败结果。
- 状态：修复候选待验证。

### REL-006（第三轮结果）

- `.local/codex/recovery-e2e-live-20261004-attempt3/` 的来源退出和 MessageService 重启两项真实恢复均 PASS；接收者只登录一次，消息 421469/421470 在真实 ACK 后为 ReceiverConfirmed。单次 ACK 52.812/58.705ms，恢复 7887.36/6343.12ms，仅为小流量恢复样本。
- 提交 `89aab6f` 保存最终执行器；前两轮 FAIL、代码提交与完整日志都保留。所有原运行容器身份与启动时间不变，私有配置不导出。
- 当前未读投影只读复查：消费者组 `tinyimx-unread-projector-v1` 已存在，近期错误中未见 IllegalConsumerGroup，看到的是闲置 MySQL 连接重连；不能继续把历史组缺失当作当前根因。
- 状态：小流量真实恢复已验证；生产候选部署、负载下恢复耗时和全功能容量仍待验收。

每个新增问题记录：问题编号、触发场景、实验编号、源码/制品身份、预期与实际、原始证据位置、根因和置信范围、改动位置、Git 提交、回归范围和结果、容量复测结果、未关闭边界。只有验证完成才从“候选”转为“已解决”。

### BUILD-001：Release umask 导致非 root 服务无法执行

- 首个候选镜像部署时 MessageService 退出 126，`tini` 报 Permission denied。构建 umask 077 产生 0700 程序，COPY 后为 root 所有，UID1000 不具备执行权限。
- 失败镜像和两次保护性停止/失败部署保留；健康超时后回滚原镜像。
- 修正镜像 COPY 的二进制权限为 0755、库为 0644，并以实际 UID1000 验证全部 10 个程序可执行、所有依赖可解析。独立修正镜像 `codex-capacity-1836cf5-r1` 已部署 MessageService 和两个 Gateway，通过健康检查；其他容器和私有配置哈希保持不变。
- 原始证据：`.local/codex/release-permissions-1836cf5-20261004/`、`candidate-deployment-1836cf5-20261004-attempt2/` 和 `attempt3/`。不修改原构建产物内容，不删除原镜像。

### PERF-004：真实 1k 负载仍出现业务排队与负 ACK

- 场景 `w1k1836a`：1,000 用户全部登录，100msg/s 持续 60s，发送 6,000 次；正 ACK 5,033、负 ACK 967，正 ACK P99 上界 2,995.2ms、最大 3,046.024ms、活跃窗口正 ACK 81.3667/s。心跳 5,394/5,394，无断连、无负载调度跳过。
- SQL 快照 5,102 条本轮消息，全部 ReceiverConfirmed；69 条耐久消息没有正 ACK，898 次发送在快照未观察到持久化，后者不能直接定为永久丢失。原始账本、SQL、日志和资源采样全部保留。
- 阶段日志显示部分 dispatch_age 约 2.4–2.7s，而开始后的工作约 40–74ms，证实明显队列等待；来源持久化阶段也有几十至百毫秒样本。当前不把单一推测定为根因。
- 开启/关闭新恢复 gate 的相同负载 A/B 用于分离周期恢复与旧消息/业务队列影响；关闭 gate 的实验不能作为恢复能力验收。数据库 flush_log_at_trx_commit=1、sync_binlog=1 保持不变。
- 状态：FAIL，待根因修复和负载复测；本轮没有新的 10k～50k 成功记录。

### TEST-004：对账器错误要求投递包携带客户端逻辑 ID

- 首轮对账器按 C/M/from/to 匹配，但实际投递协议允许省略 C，导致全部 5,033 个正 ACK 被误判为缺少投递。首轮代码提交 `efaabe1` 和原始 FAIL 摘要保留。
- 修正：投递使用稳定 M/from/to tuple 关联，C 仅用于发送、ACK 和 SQL 逻辑幂等键；不更改服务或协议。对原账本的修正分析另存，不覆盖原报告，也不改变真实 P99/负 ACK FAIL。
- 状态：修正候选，待账本重放及新轮运行验证。

### PERF-005：消息同步 I/O 工作线程被固定限制在 8

- A/B `w1k1836a`（恢复开）与 `w1k1836b`（恢复关）分别负 ACK 967/241，P99 2995.2/2969.6ms；均不能通过。关闭恢复不是完整解决方案。
- 代码和启动证据：`gateway_demo.cpp` 消息工作池 `max(4,min(8,business_workers*2))`，实际 8 workers/64 stripes/512 queue；同步聊天、跨网关验证和接收确认共享此工作池。两个网关 CPU 约 40–51%，多条已开始的工作耗时几十毫秒，而排队 2.4–2.7s。低 CPU 与长队列支持 I/O 并行度不足假设，仍需同条件验证。
- 候选：加入显式 `TINYIMX_MESSAGE_WORKER_THREADS`，仅接受十进制 4..64，非法值在资源创建前退出；未设置时维持原默认。实验用 16 workers，并重新启用恢复 gate；不扩大队列、延长 deadline、跳过权限或 SQL，不减少 fsync 耐久性。
- 对账器重放修正已验证：两轮 5033/5759 正 ACK 全部匹配 M/from/to 实际投递和 SQL ReceiverConfirmed，原始报告不覆盖。`b7cfa27` 保存修复与问题记录。
- 状态：线程配置候选，待构建、运行验证和进一步容量复测。

### TEST-005：终止连接时有一条心跳仍可能在途

- A/B 第二轮最终心跳 5312/5313，旧执行器在报告期间继续创建新 ping，然后立即断开；不能仅凭此终止快照认定那条已超时或服务丢包。旧报告原样保留，不重标 PASS。
- 候选：活跃窗口、消息 drain 和 SQL 核对完成后，协调器请求 quiesce；客户端停止创建新心跳，继续读取每一条已发送 ping 的 pong，写独立 heartbeat-drained 屏障。35 秒仍未全部响应则真实 FAIL；计数不抹除、不忽略在途请求。
- 影响：仅测试工具结束阶段，服务器配置和活跃窗口不变，所有原始请求与响应计数保留。现有运行使用原二进制，结束后才构建新版本，避免编译干扰测量。
- 状态：修正候选，待下一轮实际验证。

### PERF-005（16 workers 实际结果）

- `w1k16a`：恢复重新启用，16 workers 实际启动；1,000 在线、100msg/s、60s，正 ACK 5,379/6,000、负 ACK 621，P99 2,982.1ms、最大 3,066.105ms、活跃正 ACK 86.8333/s。SQL 5,482 条全部已确认，其中 103 条没有正 ACK，518 次发送在快照未观察到耐久记录。
- 所有正 ACK 均已匹配真实 wire delivery 和 SQL 身份/确认，修正对账器正常工作。心跳末尾 5306/5309 为未 settle 的旧口径；本轮仍因真实负 ACK 和 P99 FAIL。
- 结论：单独增加线程未能消除共享资源和长队列问题，不作为已解决根因。配置迭代 `734effb`、独立镜像和该轮失败账本均保留；仍需低率标定、共享资源/数据库等待定位及同条件复测。

### TEST-005（结束屏障实际验证）

- `8e6e386` 保存心跳 settle 改动。`low1k16a` 1,000 用户、10msg/s、30s 全部 300 条正 ACK、wire 和 SQL ReceiverConfirmed，P99 41.0ms、心跳 3119/3119，低负载私聊 PASS。
- `diag1k16a` 1,000 用户、100msg/s、30s，正 ACK 2415/3000、负 ACK 585、P99 2986.6ms，心跳 3000/3000、无断连。修正测试结束计数没有掩盖负载性能 FAIL。低率成绩不代表 10k～50k 或全功能验收。

### PERF-006：缓存不足与持续写入/查询等待需要分别验证

- `mysql-load-diagnostic-20261004/` 对同轮前后聚合取增量：物理缓冲页读取 11745 次、数据 fsync 17460 次；Outbox 状态更新均值 28.259ms，COMMIT 7.101ms，未读会话 COUNT 10.793ms，待投递接收者发现 72.662ms。等待采样多次出现 waiting for handler commit。不能使用两天历史平均冒充本轮数据。
- MySQL 8.0.40 原缓存 128MiB，应用表/索引约 630MiB。`mysql-buffer-1g-20261004/audit-before.json` 记录资源预算、零活动事务、原值、风险、非持久 SET GLOBAL 和回滚；临时增至 1GiB，flush_log_at_trx_commit=1、sync_binlog=1 均保持。没有重启、SET PERSIST、清理数据库、终止用户应用或修改 VMware。
- `buf1k16a` 1k、100msg/s、60s：6000/6000 正 ACK、真实投递及 SQL 确认，负 ACK 0，心跳 5000/5000、无断连、活跃正 ACK 99.6333/s；P99 393.4ms、最大 684.104ms，仍为 FAIL。物理缓冲读取 2708 次；会话 COUNT 均值 6.9ms，发现查询 50.896ms。
- 相同参数预热复测 `buf1k16b`：6000/6000 全部成功确认、负 ACK 0、心跳 5192/5192，无断连；P99 508.1ms、最大 797.577ms、活跃 99.7833/s，仍 FAIL。物理读取仅 4 次，说明缓存候选消除本轮负 ACK 后仍未解决尾延迟；不能声称唯一根因已修复。
- 原始 SQL 查询已有索引 (to_user_id,delivery_status,message_id) 不能同时覆盖 from_user_id 过滤；发现查询按 delivery_status=0 和 to_user_id 游标筛选。迁移 012 候选新增 (to_user_id,from_user_id,delivery_status) 与 (delivery_status,to_user_id) 两个非唯一索引，保留原索引、所有行和业务语义。
- 索引候选必须保存详细前置审计、精确定义、旧/新 EXPLAIN、同一只读一致快照查询结果等价，以及原值回滚 SQL；显式 ALGORITHM=INPLACE、LOCK=NONE、会话 metadata lock 等待 5 秒。没有活动压测时执行，构建完再复测。状态：查询索引候选，待真实验证与负载结果，不是全功能 PASS。

### PERF-006（索引候选实测拒绝与回滚）

- `5be16e6` 保存迁移 012、审计应用器和两轮缓存 FAIL。真实 MySQL 索引定义与只读一致快照的旧/新查询等价均 PASS；未读查询选择新覆盖索引，发现查询仍选择旧索引。执行计划变化不是容量成功证据。
- `idx1k16a` 同样 1k、100msg/s、60s：正 ACK 5697/6000、负 ACK 303、P99 2894.6ms、最大 3050.374ms、活跃正 ACK 93.4667/s；心跳 5000/5000、无断连。失败完整保留，新增索引还增加写入维护成本，候选不作为当前部署方案。
- `private-index-rollback-20261004/audit-before.json` 逐一定义检查，只删除本任务刚新增的两个非唯一索引；保留所有原索引、所有数据库行、迁移源码和 Git 记录。显式 LOCK=NONE、会话 metadata lock 等待 5s，没有活动压测；回滚后原始索引定义完全一致，AUDITED_INDEX_ROLLBACK_PASS。
- 回滚控制轮 `rb1k16a`：6000/6000 真实投递、正 ACK 与 SQL 确认，负 ACK 0，心跳 5000/5000，无断连；P99 1092.2ms、最大 1420.005ms、活跃 99.7/s，仍 FAIL。只有单轮索引对照和资源环境有波动，不能将所有延迟因果都归于索引；保持拒绝部署以控制回退风险。
- 当前运行保持原索引和非持久 1GiB 缓存。保留其他主机应用，实测 CPU PSI 与上下文切换明显，继续核对系统调度、健康探针、同步日志和 RPC/SQL 等待。没有关闭耐久性、消息确认或功能来获得虚假成功。

### CAP-001：当前候选真实 10k 连接保持的有限成绩

- `hold10k16a` 10,000 用户全部实际登录 nginx TCP，统一在线屏障后保持 60s，总运行 180.85s（包含 100 用户/秒登录 ramp 和 drain）。心跳 81365/81365、断连 0，连接保持场景 PASS。
- 本轮主动新消息发送数为 0，因此正 ACK P99 为 null。恢复期间观察到 204 条既有测试消息投递并发出真实接收 ACK；这不是专门故障恢复或消息吞吐验收。不能将 hold PASS 解释为 10k 私聊或群聊/文件/MCP/交叉流程 PASS，更不能外推到 50k。
- 两个 Gateway 镜像 `43a68f…`、MessageService `05380ac…`，16 消息线程、恢复开启、原索引及临时 1GiB 缓存。19 个容器没有在本轮重建；原始账本、在线屏障、资源、结束心跳屏障与结果均保存。

### PERF-007：同步日志与重量健康探针的资源开销对照

- `runtime-warn-candidate-20261004/` 在零业务连接时保存四个配置的逐项审计、SHA 和私有原文备份，仅 logger.level INFO→WARN，重启原 ID 的 MessageService、SocialService 和两个 Gateway；镜像、环境和其他容器启动时间不变。WARN/error 和慢请求 phase 仍保留，未跳过业务操作。
- `warn1k16a` 1k100msg/s60s，6000/6000 正 ACK、wire、SQL 已确认，负 ACK0，心跳5000/5000、断连0；P99 349.5ms、最大638.074ms、活跃99.9167/s，仍 FAIL。CPU20s聚合观察不是调用栈或可确定因果的 profile，不能据此宣称日志为唯一根因或永久降低日志级别。
- ZooKeeper 现探针每5s执行 zkServer.sh status；该 Java CLI 与采样中的新 Java 进程一致性仍需隔离验证。只读验证 bash直接 srvr 返回 Mode: standalone，耗时约12ms，保持旧探针的 standalone/leader/follower 判据。新 override 为候选，不直接改全局默认、不关闭健康检查。
- 计划：正/负健康查询、精确 healthcheck-only 漂移检查、零业务连接保护，保留 ZK image/env/volume、仅重建 ZK；验证所有原服务健康及注册恢复，再同1k负载复测。原 healthcheck 详细定义及 rollback override 在变更前保存；失败回滚，不删除卷或强杀。状态：轻量健康探针候选，未容量验收。

### PERF-007（等价健康探针真实结果）

- 第一轮前置 FAIL：执行器错误将 CMD-SHELL 字符串当程序名，返回127，未部署运行容器；原脚本、源码备份和失败记录保留。第二轮改用 sh -c，正探针成功、错误端口负探针失败，通过 healthcheck-only 漂移核对及零业务连接审计后仅重建 ZK。同镜像、同命名卷，其他18容器ID/启动时间不变，全部原服务健康。
- `eecbf18` 保存可选 override 与复盘。`zk1k16a` 1k100msg/s60s，6000/6000正ACK、wire、SQL确认，负ACK0，心跳5180/5180、断连0；P99 230.7ms、最大560.888ms、活跃99.7667/s，仍FAIL。前WARN轮349.5ms只是一次对照，不能外推稳定改善或全功能容量。

### PERF-008：redo 线程分配候选保留耐久性后待复测

- `mysql-direct-redo-20261004/audit-before.json` 保存MySQL8.0.40原参数和精确回滚，在零活动压测/事务时临时 SET GLOBAL innodb_log_writer_threads=OFF；事务线程承担 redo 写入/刷新，innodb_flush_log_at_trx_commit=1、sync_binlog=1、doublewrite及1GiB缓存不变，没有SET PERSIST或重启。
- 官方依据仅为工作负载相关假设：https://docs.oracle.com/cd/E17952_01/mysql-8.0-en/optimizing-innodb-logging.html 。低并发事务可受益，高并发必须逐档复测，不能减少落盘责任换性能。
- `redo1k16a` 6000计划中5999实际发送，5999全部正ACK/wire/SQL确认，负ACK0；P99 142.4ms、最大235.911ms、活跃99.95/s，心跳5000/5000。少发1条与P99>100均为FAIL；完整原报告保留。日志fsync增量14360，前轮25422，采样时长含ramp/drain，不直接当steady吞吐。

### TEST-006：窗口尾部计划请求被10ms事件等待丢弃

- `redo1k16a`原账本最后一次发送为59986.768ms，序号5999；最后计划due=59990ms没有发送，最大既有调度延迟12.164ms。源码到end即将未发计划全部记为skip，导致最后一次epoll等待跨界后遗漏尾部请求。原FAIL不重标PASS。
- 候选：在固定15s drain内继续每次最多256条追赶所有due<end的绝对计划；迟发保留原scheduled时间并独立late_offered_requests计数。活跃ACK吞吐窗口不扩大，坏连接或在途冲突仍skip并FAIL；drain结束仍未发的计划保持skip失败。
- 增加scheduled-to-ACK P99≤100ms门槛，覆盖客户端调度等待，原正ACK P99、全计划/零skip、SQL与wire等门槛都保留。状态：执行器候选待构建及真实复测；不修改服务器性能指标或掩盖任何原失败。

### TEST-006（完整计划的新轮验证）

- `b4e5bfa` 客户端构建/链接、协调器语法检查均通过。`redo1k16b` 6000计划/6000实际发送，skip0、late0，6000正ACK/wire/SQL确认，负ACK0，心跳5000/5000、断连0；正ACK P99 176.1ms、scheduled-to-ACK178.1ms、最大346.610ms，两个延迟门槛均FAIL。旧5999发送的报告保留。
- 本轮没有实际迟发，尚需专门边界验证；不得把计划完整但P99失败解释为达标。

### PERF-009：未读快照冗余往返与重复扫描

- 源码 LoadDialogSnapshot 每次BEGIN、两个COUNT、COMMIT，统计private与total；LoadUserSnapshot 已为单个grouped SELECT但仍额外BEGIN/COMMIT。实测两类读取均持有连接池，投影消费与消息持久化共享SQL资源。
- `unread-single-query-probe-20261004/` 在8个既有合成接收者的只读一致快照比较 COUNT/SUM 与原两个 COUNT，结果等价。EXPLAIN ANALYZE 单个示例合并4.23ms、原private17.2ms及total10.2ms；单个执行计划是诊断证据，不能代表P99改善。
- 候选：单 SELECT `COALESCE(SUM(from_user_id=peer),0),COUNT(*)`，按receiver及0/1状态过滤；同一个InnoDB读视图返回两个计数，校验private≤total。用户grouped SELECT原语义不变，包含历史zero-unread peer并在C++求同视图total。移除两处多余BEGIN/COMMIT，不更改isolation/autocommit、表行、索引、RPC或Redis幂等规则。
- 官方一致读说明：https://docs.oracle.com/cd/E17952_01/mysql-8.0-en/innodb-consistent-read.html 。池lease没有外部未结束事务，原池Release仍负责异常事务回滚。
- 验证计划：MySQL连接局部 TEMPORARY shadow，覆盖多个peer、全部0/1/2/3状态、其他receiver隔离、空计数、zero-unread历史peer、非法身份及无连接错误；临时表仅在本测试唯一连接可见，关闭连接自动退役。再运行原只读真实SQL回归、Release服务构建和实际容量复测。状态：源码候选，不是性能PASS。

### PERF-009（真实回归、部署和容量结果）

- `76396e4` 保存一条SQL快照候选。14项临时表断言、2项原真实只读SQL断言、27项crash-window合同断言通过。初次新Make目标未知的构建FAIL保留；单独审计CMake重新配置后第二次构建通过，没有下载依赖。
- 链接依赖核对后缩小实际部署范围：只有UnreadProjector使用该读取器，因此仅更换UnreadProjector镜像；Gateway没有重建或更新。其他18个容器身份及私有配置SHA保持不变，旧镜像及精确回滚override保留。
- `uq1k16a`，1k用户100msg/s60s，6000计划/发送/正ACK/真实wire/SQL确认，skip0、late0、负ACK0，心跳5122/5122、断连0、活跃99.9/s。正ACK P99=113.3ms、scheduled-to-ACK P99=116.6ms、最大211.797ms；两个P99门槛仍FAIL，不重标达标。
- `unreadsingle-1k-projection-parity-20261004`：对所有1000用户的2000个私聊/总未读Redis键只读核对SQL，首次检查全部一致、缺键0、差异0；没有人工修复缓存。投影正确性PASS与容量P99 FAIL分开保存。

### TEST-007：从私聊基线扩展到真实跨功能链

- 新增显式persistent-TCP actor，限定四个经SELECT验证的既有合成账号。非相邻好友测试对必须没有任何既有关系/请求；通过正常公开协议创建/接受/拒绝请求，随后衔接私聊投递、幂等、历史、已读、会话列表、全部群管理及群消息真实ACK、文件会话与实际RPC上传/分段下载。
- 新建测试群的解散和自己新上传会话的取消是明确审计的正常功能操作；不执行SQL写入、既有账号重置、任意文件/表行删除、缓存修复、服务重启或用户进程终止。文件流阶段只复用实际客户端prepare/resume，无服务重启，不冒充故障恢复测试。
- 每次新目录记录审计、请求/响应序号、业务成功/明确负向权限检查、SQL权威状态、实际wire身份/内容、每操作样本延迟、未完成失败。单链每操作样本数有限，不作各功能容量P99达标结论。TLS/MCP/离线/故障/长稳态仍需要单独补齐。
- 用户最新指示保留其他应用，在当前资源下测试；未暂停/结束Python或游戏。原一小时只读主机监控正常结束后，新审计监控于09:08UTC启动，PID24464，Hidden窗口，仅CIM读数。

### TEST-007（首轮真实链 FAIL 与合同修正）

- `mixed10ka` 在全部10000用户在线、100私聊/s背景中完成17操作，好友双向关系与拒绝无关系、真实私聊M/from/to/text/wire、重复发送同M、SQL已确认全部通过，历史内容断言失败；原FAIL/响应/账本完整保留。
- 真实history回包content为持久化JSON envelope `{from,text,to}` 字符串，Gateway源代码server_body.dump()存储并由history原样返回；首版测试误认为plain text。仅修正测试：按M找到唯一项，检查外层from/to/type/state，再JSON解码内容对三个字段精确比较。生产协议/存储代码不改动。
- 重跑必须选新的非相邻、无既有关系/请求的测试对，不能覆盖已产生的首轮数据；新run保存新HEAD及原始失败。后台10k协调器与worker不修改，其启动时的源码身份保持可追溯。

### TEST-008：群禁言公开UTC时间与SQL内部格式不同

- `mixed10kb` 完成32操作；修正后的完整history内容、私聊已读SQL2和会话unread0、群创建幂等、加入/邀请、version更新、两页成员恰好4人、我的群及管理员设置均通过。群禁言的SQL-style输入被真实服务拒绝为invalid_group_request，整轮FAIL保留，文件/后续群操作没有标PASS。
- GroupApplicationService.NormalizeMuteTimestamp及现有集成测试要求公开UTC格式 `YYYY-MM-DDTHH:MM:SS.000Z`，内部才规范化成SQL datetime。测试脚本现使用明确UTC毫秒格式，另外保留错误SQL-format输入必须拒绝的负向断言。业务服务没有修改。
- 下一轮必须用fresh四账号519820/822/824/826；之前的好友/群历史保留，不自动清理或重置。链测试的逐操作样本仍不能代表P99容量指标。

### TEST-009：负向断言应检查公开错误码和权威状态

- `mixed10kc` 完成34操作：SQL-format错误时间正确拒绝，RFC3339UTC正向禁言成功，随后真实群发被拒绝。测试误猜测reason/message应含mute，实际Gateway合同为`group_send_permission_denied`，因此整轮仍FAIL保留；不能当业务禁言失败，也不将未执行功能计为通过。
- 测试改为精确公开reason，同时SQL验证成员仍active且muted_until在未来，拒绝消息没有任何持久化行；unmute后必须真实成功投递并ACK。增加每操作monotonic边界与可选背景run的start/end约束，保证声称在10k稳态内的操作全部落在真实原负载窗口中。

### TEST-010：文件会话按所有者隔离并隐藏存在性

- `contracts4` 在10k背景测试已经结束后的独立四用户链完成47操作。全部群管理操作、UTC禁言与负向无持久化、解禁后的3份真实wire及SQL delivery_status=3、消息幂等、成员离开/重邀/踢出、群主转移和解散均通过；文件创建/幂等/本人的会话读取通过。
- 他人查询真实上传ID返回file_upload_not_found，首版断言误期望file_permission_denied，整轮FAIL保留。源码owner-scoped查询隐藏其他所有者的会话；修正精确notfound，并检查不泄漏file/session字段，随后本人仍可取消及复用取消结果。不能因为否定响应而掩盖timeout/unavailable错误。
- `uq10k16a`完整300s基线：30000计划/发送/正ACK/wire/SQL已确认，负ACK/skip/late/断连均0；心跳243988/243988，活跃99.9567/s，ACK P99=292.0ms，scheduled-to-ACK294.6ms，两个延迟门槛FAIL。最初17操作链FAIL与后续链各自保存；原联合报告不修改为PASS。

### TEST-011：真实完整功能链通过与容量账户边界

- `contracts5` 独立四用户链49次公开操作PASS；好友到私聊/历史/已读/会话、全部群管理与三接收者SQL3/真实wire/ACK、文件开始/会话/他人隐藏/取消及幂等、真实RPC上传/Finalize/分段下载续传和逐字节一致均通过。28类request外还有私聊/群deliveryACK；TLS/MCP/离线/故障/长稳态及各功能P99容量仍NOT_RUN，不能把独立链称为10k全功能达标。
- 本次链短于首个5s心跳周期，原报告0/0的心跳比较是空断言；不改写原报告。后续actor强制每人发送一个真实ping，并要求nonzero+全部返回，还补记实际容器镜像/启动身份。10k基线的243988真实心跳不受这一测试缺陷影响。
- 旧coordinator硬编码500000/m21b500000_而旧fixture只有20k；unused的capacity-owned-range初稿审计没有应用，后续版本重新绑定当前HEAD。新增显式base/prefix并统一SQL/worker；预检失败有独立FAIL记录，重复run不覆盖原结果。
- 新seed工具默认SELECT-only，准备验证700001..750000及codex50k_20261004_名称范围为空，再strictINSERT50k新合成账号/100k互为好友ring，<=1000行/事务；绝不重置旧账号密码/status/关系，私有batch完整保留，部分失败不自动删数据。自动递增可能升高而被审计保留，不能调低。状态：fixture仍未创建，真实50k认证/性能待测。

### TEST-012：合成数据最终核对的无符号边界

- dry-run `fresh50kplan`确认目标ID/名称及17个用户外键引用范围全空，无活动ownedworker。只读`collisionguard`对旧范围按预期FAIL且completed_batches0，证明不会覆盖既有账号。
- `fresh50kactual` 150批INSERT全部提交，50k新用户/100k关系精确计数通过；所有旧28939用户ID/昵称/avatar/status/密码salt+hash及115561旧关系的前后SHA均完全一致。最后ring验证表达式`uid-base-2+N`在最小unsigned UID先减2导致真实ERROR1690；原seed及联合保护报告均FAIL保留，没有再插入、重置或删行。
- 只读诊断将计算改为`uid-base+N-2`，相同真实表返回100000。新`--verify-run`只读模式绑定原seed审计参数、每批私有SQL SHA及完整完成列表，再核对PBKDF2合同、全部canonical账号和exact ring。旧FAIL不改写；新证据另存。实际认证/性能仍待测。

### CAP-013：新50k数据核验与规模实测失败

- 后续 `fresh50kverify` SELECT-only核验PASS：50000用户、100000互为好友ring；旧数据字段哈希未变。早期AUTO_INCREMENT读取命中了information_schema缓存；仅当前SQL会话关闭统计过期缓存后，真实AUTO_INCREMENT=750001、MAX(user_id)=750000。未重置计数器，旧缓存值不能作为实际插入后的计数。
- `nf10k16a`：10000全部登录，60s/100消息每秒，6000计划/发送/正ACK/wire/SQL确认，负ACK/skip/late/断连0，HB82785/82785。P99=395.0ms，scheduled399.0ms，活跃99.5833/s；延迟FAIL。49操作链在同一原始10k稳态窗口内PASS，每人HB2/2；低样本功能正确性不能代表各功能P99达标。
- `nf20k16a`：20000全部登录，6000发送，4866正ACK、1134负ACK，HB230854/230854，活跃79.9333/s，P99=2967.4ms、scheduled2968.3ms，FAIL。交叉功能链第15操作真实失败，relation_service_deadline_expired，权限RPC预算耗尽，stored_persistent=false；后续群与文件NOT_RUN。
- `nf30k16a` 登录中止，10352/30000已认证；`nf50k16a` 登录中止，8662/50000已认证。未进入全在线稳态，业务发送0；不能声称已完成30k/50k性能测量。所有19容器仍运行，无OOM/重启，其他应用保留。
- 601条限速采样的10k慢请求中persist阶段中位85.096ms，dispatch1.475ms、permission12.839ms；样本含ramp/drain，不代表全体P99。MySQL20k窗口outbox发布UPDATE平均29.001ms、累计锁等待51.022s；仍需分离连接池等待、RPC排队与数据库耗时，不能据此认定唯一原因。

### TEST-014：失败响应与中止摘要观测缺口

- 旧worker登录失败只抛LOGIN_IDENTITY_OR_FAILURE，负ACK只计数，丢失reason/message；旧coordinator中途异常没有summary。原失败证据保留，补充工具不改变速率、截止时间或验收门槛。
- 新worker仅在失败时追加allowlist响应字段至failure-responses.jsonl，最大512字节/字符串、合成密码脱敏，不保存原始body。新coordinator在owned workers退出后汇总部分认证数、全在线标记、原窗口与原因计数，明确中止不是容量通过。
- 验证要求：真实合成错误密码必须产生失败原因；随后相同100用户/s条件重现30k登录问题。单独归档旧binary，单线程编译新测试binary；没有应用服务部署。结果另存，不修改旧FAIL。

### REC-015：数据库/缓存连接槽位在失败重连后丢失

- 源码Acquire弹出连接后，Ping和Connect都失败时直接返回空lease，unique_ptr销毁、AvailableCount永久减少而Size不变。MySQL/Redis均有该路径；重复瞬态故障可能耗尽所有槽位。MySQL Connect另有mysql_为空立即拒绝的前置条件，Close或失败Connect后无法再初始化。
- 先新增显式opt-in组件探针，以真实MySQL/Redis连接经过自有loopback relay。故障仅关闭测试连接，执行SELECT1/PING、空事务，三个连续故障Acquire、恢复后无需重新Initialize及Shutdown持有lease不复活槽位。共享服务、业务行与其他应用不改变；真实密码配置仅在runtime-private，输出仅白名单测试标记。
- 当前修复尚未应用，先保存原实现真实FAIL，再按独立审计修正并对照重验。正确性修复不自动代表容量或P99改善。

- 原实现`pool-recovery-original1`已真实复现：MySQL关闭后重连FAIL，三个故障Acquire均丢失slot，恢复后SELECT1无法通过；Redis同样三次slot检查FAIL，恢复PING不能成功。共享服务未停止，所有失败保留。
- 本次候选删除MySQL空句柄拒绝前置条件，Close后总是mysql_init；两种pool重连失败时经Release归还slot再返回空lease，Shutdown已有锁/guard保持。不会重放业务事务，也不把失败请求当成功。组件fixed1待对照验证，运行镜像尚未部署。
- `hold30kreason1`同100用户/s登录-only诊断到28230/30000，三个worker最终RECV_ERRNO_104，未达到all-online、没有稳态业务发送。入口与Gateway无重启/OOM、nofile262144；选定nginx错误类别均0。不同于原nf30k的较早登录拒绝。需要验证Docker发布端口NAT连接元组限制，尚不能断言数据库/P99修复即可解决。

- `pool-recovery-fixed1`全部36项真实连接检查PASS；空事务自动回滚、Shutdown持有lease不复活、三次故障槽位保留及恢复无需Initialize均通过。另14项snapshot和27项crash-window回归PASS。尚未部署运行镜像，不能把后续入口对照变化归因于此源代码修正。

### TEST-016：同机发布端口的压测客户端路径

- 自有两个loopback源127.0.0.2/.3通过127.0.0.1:9000登录均成功，但nginx看到的对端都为172.18.0.1。原30k在28230认证后同步reset，与guest临时端口32768..60999范围大小28232接近；这是待验证的入口客户端瓶颈假设，尚不是唯一根因证明。
- 绑定loopback源访问192.168.220.128:9000超时，原FAIL保留。随后只使用已存在的192.168.220.128、192.168.220.129、192.168.58.129、172.18.0.1、172.17.0.1，每个都成功登录真实nginx containerIP以及实际发布的guest-address:9000。没有增加地址或改变routes/firewall/sysctl。
- coordinator新增显式host/source-ips，override必须是本机现有IPv4，来源唯一且足够每worker一个；记录原CLI和worker源地址。默认旧路径不变，负载速率100用户/s、deadline和门槛不变。先2用户正常hold验证，再30k/50k同条件login-only测量；不把hold成功等同全功能容量。
- 实际MCP鉴权负向401及授权discover/list9工具通过，但static_user_id=1在真实库不存在，profile业务返回HTTP200/isError=true/not_found。AIagent未运行；实际配置qwen3:8b/host.docker.internal，仅检查配置未调用provider。MCP业务和AI容量不能计为PASS；生产身份修正需要明确合法principal，先用独立合成账号验证完整工具链。

### CAP-017：单个nginx worker连接预算不足

- published2用户hold PASS，HB4/4；pub30khold1在26428认证后reset，pub50khold1在27605认证后新登录连接断开，都没有all-online或业务稳态。原失败和两种客户端入口对照保留。实测nginx peer已经分成四个地址，不能把原loopback聚合当作唯一根因。
- 后续有界Docker日志读取捕获两次明确alert：10:57:07及11:01:48，worker29的`32768 worker_connections are not enough`。worker配额包括客户端与上游连接，accept分配并不均匀；nofile262144不能代替worker配额。官方说明：https://nginx.org/en/docs/ngx_core_module.html#worker_connections。
- 候选只将worker_connections提高到131072，其他nginx参数不改。为一个worker承接全部50k客户端及50k上游预留空间，同时小于nofile262144；保守要求额外1GiB可用内存预算，实测RSS/guest资源另存，不能以配置值推断实际内存占用。
- 首先将候选复制到自有容器/tmp路径并nginx-t；确认无ownedworker/business连接后，原config保持inode写入以让readonly bind看到内容，白名单源码提交，随后单独审计graceful reload。所有19容器身份/镜像/start必须保持，无重建或删除。原config精确备份；失败时同inode恢复、nginx-t/reload并另记失败。
- 一个诊断tail误读error.log的/dev/stderr symlink而阻塞；只核对并停止本任务exact tail reader，容器无pgrep/外部kill导致的两次工具失败也保留。后续所有读都有超时，拒绝设备/流。该诊断失败不能当成nginx错误计数0的证据。

### DIAG-018：持久化事务数字阶段计时

- 真实MessageService两个fault-delay环境变量都UNSET；85ms阶段不是残留人工延迟。新增默认OFF的数字计时，明确precheck、获取连接(含Ping)、BEGIN、INSERT、identityread、outboxinsert、COMMIT、recoveryread/ROLLBACK耗时，8条/秒限速，不含C/内容/账号密码/token。
- SQL顺序、身份核验、幂等冲突、事务与outbox原子性及结果不改；计时不会延長截止时间或隐藏失败。关闭时不调用诊断时钟/sink；测试覆盖move-only返回、void、异常原样传播、重复阶段累计、未执行-1、数字结果、慢失败采样和限速。后续构建/部署须等owned capacity停止并另审计。
- nginx已预验并保持inode平滑重载；19容器ID/image/start及应用私有configSHA全部保持。记录nginxMEM343.6->448.3MiB，低于额外1GiB保守预算。ng30khold1真实30000认证、30s保持、HB380377/380377、断连0，PASS。50k还在运行；这不代表私聊/其他功能P99通过。

- 后续`ng50khold1`真实50000全部认证，30s保持窗口，HB971681/971681，断连0，PASS；总wall553.149s含500s ramp与drain，不是553s全员在线稳态。无新私聊发送、ACKP99为null。nginx单参数修正消除了本轮明确入口预算故障；不推断全部业务容量通过。
- 计时候选构建PASS：12项数字trace、49项MessageApplication、25项真实MySQL只读Resolve检查，0FAIL；binarySHA=a40c9a6c5ac97fafea456610d50255fa3f1cba351295231ec23d65f11c9830ef，C++构建HEAD6674787。尚未部署。

### TEST-019：压测运行镜像身份需显式绑定

- 新coordinator增加message-image exactSHA参数，默认仍为原05380镜像，Gateway43a与健康门禁不变。不能绕过镜像检查或把旧服务压测当新候选结果；错误SHA必须预检拒绝并保存FAIL。
- C++binary构建commit与后续coordinator/docs commit分开记录。新image必须保留真实compiledrevision6674787及binarySHA，不把纯测试文档HEAD伪称重编译版本；部署对比只允许源头后的Python/docs差异，其他C++源码不变。先旧运行服务1k100/s60control，再独立审计部署MessageService、同参数重测和逐规模真实功能负载。

### TEST-020：较小压测环的好友关系缺少反向闭环

- `pctrl1k1`旧MessageService对照1000用户、100消息/s、60s：6000发送、5994正ACK、6负ACK，HB5217/5217，P99=349.5ms、scheduled354.1ms，FAIL。保存的六条回包全部是701000发向700001的not_friend，每轮1000消息一次；不是已证实的持久化失败。
- 新50k原始数据是双向ring；coordinator切出较小用户范围时仅补最后用户到首用户一条边，实际权限要求互为好友。修复枚举所有测试ring边及反向边，UNION去重两用户情形，只INSERT审计中明确不存在的合成账号关系。任何已有非好友状态，包括0/blocked，都拒绝；不覆盖、删除或重置关系。
- 本轮仅Python/docs变更。先在同一旧运行镜像05380下重新对照，保留原FAIL和P99，不把消除夹具负ACK当成延迟通过或新MessageService优化成果。后续候选仍绑定compiled667与binary/image SHA。

- `pctrl1k2`修正夹具后旧镜像6000发送/正ACK/真实接收/SQL确认、负ACK0、HB5261/5261，P99=269.7ms、scheduled270.8ms，仍FAIL。MessageService55d单服务部署核验其他18容器和全部私有配置未变。`ptrace1k1`候选6000正ACK/确认、负ACK0、HB5230/5230、P99=292.6ms、scheduled295.3ms，仍FAIL；一次对照不能证明改善。

### PERF-021：接收确认重复读取与业务工作池排队

- Message55d矩阵`nm10kp1`10000认证，6000正ACK/确认/接收、HB82464/82464，49操作链同窗口PASS，但P99=934.7ms。`nm20kp1`20000认证，4741正ACK/1259负ACK、HB230504/230504、P99=3002.1ms，功能链16操作后权限预算耗尽。源代码／数字限速采样显示20k chat dispatch中位1324.011ms，不能当全体P99。所有FAIL保留。
- Gateway接收ACK先GetPrivateMessage，再ConfirmReceiver；后者在MessageApplicationService再次读取并校验持久化recipient及Pending/Confirmed/Read/Failed状态。跟踪器已用private domain/M/认证账号/D验证已注册尝试。候选删除前一个重复读取，生产helper仅在kConfirmed或kDuplicate时调用一次原ConfirmReceiver，其服务端持久化身份校验仍保留。
- 未知消息、foreign receiver、未注册D、zero字段和group domain均不能调用确认；不确定RPC失败后本地ACK保持单调，重复有效ACK继续修复。已登记旧retry序号仍是有效接收证据。新目标有10项真实跟踪器／生产helper边界测试；后续构建、端到端验证和性能对照另存。当前30k/50k55d矩阵不部署这项变更。
