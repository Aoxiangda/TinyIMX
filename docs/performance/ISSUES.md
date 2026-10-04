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
