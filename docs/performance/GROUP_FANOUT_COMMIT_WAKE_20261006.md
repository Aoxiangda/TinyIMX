# 群消息提交到真实交付的延迟瓶颈（2026-10-06）

四轮真实交互每组1条新消息/3名收件人，严格相同MID/群/发送者/内容：
|case|请求→发送ACK ms|请求→全部收到 ms|ACK→全部收到 ms|
|---|---:|---:|---:|
|A1|23.949403|148.571237|124.621834|
|B1|20.079300|694.784020|674.704720|
|B2|25.722936|449.170399|423.447463|
|A2|23.966386|556.331727|532.365341|
只有4消息/12收件事件，不是P99、不是所有群容量。群SQL JOIN候选ON/OFF切换的结果不能解释这个发送ACK后的延迟，更不能把20ms ACK当作群交付已达标。

实际两个Gateway均启用扇出，未配置RECOVERY_MS，原入口默认1000ms。GroupFanoutCoordinator每次RunOneIteration后固定等待这个周期，新提交没有通知。MessageRepositoryAdapter在一个事务内提交message、全部recipient delivery rows和outbox，成功ACK随后才返回；群投递直接claim这些durable rows，MQ outbox不是这个直接投递的前置。定时等待是明确的机制瓶颈，具体全部时间组成与改善幅度仍由同ELF OFF/ON实测确认。

## 默认关闭的修正
TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE仅精确1启用。Gateway收到非冲突、正MID的持久化RPC成功后给本进程coordinator一个合并通知；在取消检查前通知，已提交后客户端断开仍可交付。不保存消息队列/内容/IDs，仅有界generation与CV。通知不是投递依据，仍执行原claim RPC、唯一租约、跨网关路由、收件ACK及状态fence。
保留原1000ms恢复轮询。提交结果不确定、无通知、进程崩溃、通知丢失均由原durable恢复补偿。满批最多4轮后让出25ms，避免固定1s分批等待；batch64/租约5000/ACK重试3000/失败重试1000/数据库持久化/身份与权限均不变。关闭时原等待路径保持，正常运行无周期缩短。
原生测试覆盖原2个claim/dispatch/complete及非法依赖测试、唤醒睡眠线程、提交在claim期间不丢、2000通知合并、无通知定时恢复、满批原租约完成、prompt stop。待隔离构建/原生/真实群交付控制，不声称已优化上线或万人达标。

## 已完成隔离构建与实际收件 ABBA

默认OFF、精确1开启及非法值true三种状态，同一原生ELF每状态6项测试，共18项PASS。借用已接受的main/cache-unread归档与链接对象，仅替换GatewayServer和GroupFanoutCoordinator两个编译单元；原始日志宏保留、Redis诊断不开启。构建镜像5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405；Gateway ELF e68731562d83b3b5c1923d80ed7ad4459d2f5cbc9b8a224e32014626fac04cfc。原接受归档/main/ELF未覆盖。
第一构建前置审计错误要求静态Gateway归档只引用一次；实际已接受链接顺序两次引用同一归档。断言发生在新构建目录创建/编译/运行切换之前，记录失败后仅改为“两次引用、一个唯一且精确的归档”，保留原链接顺序。修复Git facb85a；构建helper a9a8cc8；候选代码3ab92b4。

Git4443175控制源码，stage group-fanout-wake-endpoint-control-20261006。原固定group26两名已核对自有成员519870/519872，各固定连接不同Gateway，交替2条/秒。每case70条新唯一CID消息（10warm+60measured），同ELF OFF/ON/ON/OFF；不重试、跳过、放宽原3秒deadline，原1000ms恢复/5000ms租约/64batch/3000msACK重试保持。逐条seq/CID/MID/群/发送人/内容/实际收件人以及SQL delivery_status=3核对；实际ACK发送由原Actor完成。

|case|发送ACK mean / 样本P99 ms|实际收件 mean / 样本P99 ms|ACK→实际收件 mean ms|
|---|---:|---:|---:|
|A1 off|16.698541 / 49.072235|298.755233 / 601.092800|282.056693|
|B1 on|16.808037 / 27.121857|27.824996 / 40.625185|11.016959|
|B2 on|14.612380 / 23.386361|24.409283 / 41.535609|9.796903|
|A2 off|15.617606 / 37.724140|329.221884 / 780.649281|313.604278|

开启两轮的实际收件均值和样本P99均优于关闭两轮；保守地用最慢ON对最好OFF，样本P99减少93.090%。每case60个样本，nearest-rank P99恰为最大值，不能外推总体P99/万人容量或全群成员数量。群发送ACK与实际交付是两条独立指标，不以ACK冒充交付合格。
280新消息全部真实收件及durable确认，0重复wire/负ACK/超时/跳过；每case另2次幂等重发正确reused原MID、2次非成员/注入actor发送被拒且SQL0新行。数据库确认采用测量后批量快照核对，未将批量观察时刻伪称每消息实际确认延迟。

四个独立新自有4actor集合的原54操作/37断言公开交互链全部PASS，共216操作148断言，涵盖好友同意/拒绝、私聊发送/真实ACK/历史/读/未读、群分页/角色/禁言/离群/踢人/解散及3实际收件、真实1.8MiB文件流和字节SHA/幂等取消。低样本正确性仍不代替全部功能并发性能。
最终恢复原a2bb两Gateway及ca01b0 ELF，严格原全Env/健康/Cmd/HostConfig/挂载/SQL双1不变；其他17实例身份及私有配置SHA保持。当前GW A dba2aeedf6ed814a93ba7f58cea7cf1ecc1e740c02bdd2182ba70b39f8d85c7a，B a1c0c2801e769d6f9b29a890f1f8366a0cbdc19f95c10c9b170544229b054b82。候选未做万人混合/热点群最终接受，全部功能极致目标继续OPEN。下一轮将群实际收件加入原10k私聊/会话页负载并保留完整交互回归。

## 10k混合 ABBA 已完成，准备选择运行版本

Git f4fd1e7；stage group-fanout-wake-mixed10k-20261006。10000原自有环用户，总登录100/s，原private100/s×60秒=6000/case，固定50会话页20/s×60秒=1200/case。加入固定到不同Gateway的两名真实群成员2/s（70消息/60测量）和54操作/38断言（多出的1断言严格所有操作在同一steady）。群/列表/环/4功能actor互不共用登录身份，保留原native和coordinator SHA、heartbeat15秒及全部原deadline/durability。

|case|原private ACK P99直方图上界 ms|完整50项会话页 P99 ms|群实际收件 mean / 样本P99 ms|
|---|---:|---:|---:|
|mixed-A1 off|74.0|47.436211|478.139298 / 1036.300845|
|mixed-B1 on|57.5|38.293330|39.097486 / 84.044447|
|mixed-B2 on|50.9|34.198594|37.832799 / 79.622417|
|mixed-A2 off|57.7|40.925379|525.406253 / 1021.918781|

开启两轮的原private全部gates、列表sent与scheduled P99≤100ms、群实际与scheduled样本P99≤100ms均PASS。关闭两轮的群交付性能FAIL保留；保守最慢ON对最好OFF，群实际交付样本P99减少91.776%。2/s、每case60样本的P99仍等于最大值，不能外推所有群规模或总体P99。private/list数字用于回退检查，不把微小差值宣称新独立私聊根因改善。
四轮24000private正ACK=SQL=真实wire=确认，4800整个列表响应准确，280群消息真实收件与SQL状态3，0跳过/超时/重复wire/负ACK；216操作152断言（含4个steady窗口检查）正确，所有功能链和群收件在原60秒窗口内。通过这些选定场景，不等于28类型每类高频容量、50k/TLS/故障/soak验收。

测试最终先严格恢复原a2bb/全Env及原健康/Cmd/HostConfig/挂载，其他17和私有配置保持。后续选择5c2645候选ON作为继续迭代版本，须以accepted-group-fanout-wake-20261006/summary.json实际运行receipt为准；本次提交只保存明确准入条件与部署助手，未将准备描述为已经运行。完整a2bb原配置/映像回滚继续留存。
源码审查发现大群另一潜在开销：ClaimGroupMessageDeliveries在同事务内每收件人一次租约UPDATE，协调器又逐收件人完成同步RPC；当前两人群结果不证明大群已快。下一步用自有多成员群实测各阶段，再选择有界合并方案，保留FOR UPDATE/SKIP LOCKED、租约、受认证会话、实际ACK和持久化原语。20k之前private201ms/诊断149.4ms FAIL、AI秒级与文件并发容量仍OPEN，不停止全部功能迭代。
