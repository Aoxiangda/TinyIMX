# 群信息与成员授权快照候选（2026-10-06）

状态：源码候选，尚未构建/实际部署/性能验收，不将网络往返数下降当作全部功能已优化。

审查发现GetGroup、ListGroupMembers、CheckGroupSendPermission、PrepareGroupMessageSend在同一连接内逐次读取group与actor成员，两次有界主键SELECT。新增FindGroupAndMemberOnConnection一次24字段LEFT JOIN，保留14字段群记录+9字段成员记录原解析器并显式成员存在探针；不筛掉left/kicked/role/status，不省略权限判断。两条SELECT变为一条，服务器仍查两个表；不是减少持久事务、认证、业务功能或增加缓存。

TINYIMX_GROUP_ACTOR_SNAPSHOT_ENABLE严格1才启用，默认OFF保留原分支。只四条读路径，写操作原FOR UPDATE、幂等记录、membership epoch、outbox和提交不变。ListGroupMembers原BeginTransaction/commit边界保留；send权限/收件人准备原eager consistent snapshot、DB禁言时间查询、recipient选择与read-onlycommit不变。不把GetGroup原两次读宣称为已有一致快照，JOIN候选加强单次读取的一致性。

预审计实际149组仅现有自有500001–520000测试用户数据：活跃/解散/群不存在、成员不存在、owner/admin/member、kicked；无active muted/left样本，必须用正常公开交互补充，不能伪称已覆盖。前一只读helper服务名假设在SQL/证据stage前被guard拒绝，原脚本和失败保留，attempt2只修正实际docker ps确认group-service-1。

计划：实际数据库原2SELECT/新JOIN逐字段比较，null/closed/invalid输入与所有lease回收；同nativeELF OFF/ON/ON/OFF四组四接口全结果比对和分接口wall，明确closed-loop不是万人容量。旧integration有直接DELETE清理，禁止在原业务库原样运行。使用现有自有记录的只读新测试，不覆写/删除数据，再仅group-service候选健康切换、全功能公开操作/权限/禁言/退群交叉回归、真实固定群页同ELF对照。原19、private configs、其他18服务、原Group镜像/Env/Cmd/HostConfig/mount恢复配置、SQLdurability1/1/1/0/0、宿主游戏和应用保留。每一步独立审计/Git和新证据stage。

Redis935采样不支持盲扩池，commit长尾还未解决；本群候选是独立有界读往返优化，不宣称可以单独解决私聊20k P99或全部功能10k–50k目标。

## 原生回归完成与打包失败（首轮）

group-actor-snapshot-build-20261006三处TU/两archive副本/Group与native ELF全部编译链接成功。八组pool1/4同ELF OFF/ON/ON/OFF每组482检查，合3856 PASS；149逐字段group/member corpus、四接口完整行为611行/组全部等价（8组合4888），分页1/20/100/0/101与cursor边界、null/closed/zero参数、lease回收和本任务shutdown保持。3200逐接口样本原样保存；不是Gateway/10k–50k业务容量。

原生时段波动仍在：pool1 GetGroup B2 P99 2.086436ms，高于A1 1.950633/A2 1.488125；Prepare B1 5.645296，高于A1 5.081486。不能挑pool4改善数字当四接口极致收益。成员列表两pool候选P99均较两控制低，仍只组件证据。必须下一真实端到端对照及交互回归。

首镜像FROM使用物理image SHA，Docker将它误解为docker.io/library/sha256:...，metadata引用失败；未生成候选镜像，未部署，不是C++回归失败。原Dockerfile/log/全部成功对象及八组数据保存。已只读确认实际已有tinyimx/runtime:m21-final物理ID38dca459...与Group原镜像相同；新attempt2仅新目录引用此tag，build前后核对physicalID，无pull/SDK安装。7个成功产物在首失败后、复用前精确SHA冻结，不伪称为原编译前记录。新镜像v2/加载probe本提交时NOT_RUN。

## 首实际控制守卫中止与恢复复核

group-actor-snapshot-endpoint-control-20261006在首次OFF切换健康后，被HostConfig严格JSON守卫中止，endpoint/cross操作均0，不能算协议或性能结果。finally恢复原Group镜像38dca459/ELF8fe7eab8及全部Env后，再因同一表示守卫中止。所有original inspect/override/切换与恢复日志/failed.json保留。只读group-control-abort-review-20261006确认19 healthy、other18 IDs/images/start精确保持、Env/mount/Cmd/User/health相等、原Group ELF正确；当前GroupCID130c810f703db21f362d6674240d439a73e6a2774a969845dd5b5bf6590100ec。

只读group-control-dns-representation-20261006确认HostConfig仅Dns/DnsOptions/DnsSearch原[]、恢复后null，其余全部字段精确相等，无具体DNS值变化。新attempt2只将这3项的null规范化为[]，任何非空值和全部其他HostConfig仍精确比较；不是忽略DNS/资源/安全/挂载。新stage、新runid，原失败不覆盖；核对最新GroupCID并保留other18，不假设preflight旧CID仍当前。wrapper原54操作/37断言、期限/字段/全部功能测试不变。新控制本提交时NOT_RUN，候选未保留运行或接受性能。


## 实际端点 attempt2 中止及注册时序

OFF GetGroup100测量成功，成员页第97个失败group_service_unavailable；ON及四功能链未执行，整轮保持FAIL。旧会话在失败前约9ms过期，新进程失败后约63ms才报告注册完成，nc提前判定健康。原Group恢复。详见RPC_READINESS_LIFECYCLE_20261006.md及原请求/Group/ZK日志。先修正五服务共用信号继承和就绪缺口。
