# 文件初始化幂等重读优化（2026-10-06）

状态：源代码93e2e6a9已保存；150真实隔离SQL及原应用层测试PASS。真实入口第二轮FAIL，尚无有效ABBA性能结论；原运行已完整恢复。当前准备带已验证注册就绪的同ELF下一对照。

实际源码BeginUpload先FindByClientUploadId获取完整owner-scoped JOIN bundle；存在时ResolveExistingUpload再次同key查询。新上传路径则保持原事务、UserExists、file INSERT、server-generated storagekey UPDATE、session INSERT/readback、COMMIT；唯一键败者与不确定提交必须重新读durable结果。本迭代只去除正常已存在请求的第二次读取。

strict defaultOFF开关TINYIMX_FILE_BEGIN_UPLOAD_SNAPSHOT_ENABLE=1。在found路径将本请求刚读出的完整结果送入原状态/身份/ToView/fingerprint校验，不缓存跨请求结果。原OFF重新读以及新创建/竞争败者/不确定提交的fresh resolver保持。完整ToView必须校验file/session IDs、owner、total_size及状态一致，冲突仍不返回bundle；Get/chunk/finalize/download权限与状态读取不变。

并发状态语义：结果取自本请求precheck SQL快照；原第二次无锁SELECT也不保证回复时最新状态。取消/完成恰好并发时Begin可能返回先前有效状态，后续有状态要求的操作仍按原durable读取验证，不因优化获得权限或写已取消文件。需真实并发Begin+Cancel验证，不能伪造强一致latest保证。

原生待验证六个absent自有schema OFF/ON/非法开关 pool1/4，原始FindSQL及健康PING每幂等调用2→1计数；新创建/引用反斜杠字节/owner隔离/fingerprint冲突/无效bundle/SQL错误/唯一键8并发无孤儿/原取消与状态；提交前回滚和真实提交后模拟错误必须fresh读取恢复。80次每模式原样记录，不表示生产P99或容量。wrapper只测试链接，模拟返回错误不等同实际断网。

不删除任何业务文件或SQL记录；所有own schema、失败、前像与Git保留；实际文件分块/终结/恢复下载/字节正确性和公开链后续仍必须在候选对照验证，全部功能20k–50k极致性能OPEN。

## 真实SQL和构建记录

150项checks=25×OFF/ON/非法01×pool1/4全部正确，原应用层测试原源码也PASS。包括quoted CID、owner隔离、fingerprint冲突、完整record校验、8并发仅1创建/7复用且无孤儿、并发取消/后续durableGet、SQL错误整回滚、提交前失败和真实提交后模拟错误仍fresh恢复。模拟错误是testwrapper，不等同实际断网。80原生重复调用OFF均值3723.6875/3432.825us，ON1552.800/1489.750us，非法值3264.2625/3328.375us；SELECT/PING均2→1。仅仓库层480样本，不作为生产入口P99或10k50k容量。

第一次构建原应用层测试cached cpp.o不存在，native新测试已编译/链接但未执行，任何schema创建之前失败。新attempt2自行编译原测试源，缓存和第一失败完整保留，6自有schema及所有夹具保留，生产配置/原持久化/19实例没有变化。候选v1镜像32e18fa...、ELF2e1f9e...，只adapter对象与原cached其他对象链接，无testwrapper进入服务。

## 两次真实入口中止保持FAIL

第一次FileOFF替换的保护断言失败，仅HostConfig.Binds列表顺序不同；Mounts内容、源/目标/权限完整相同。测量请求0。原镜像38dca、f1b9 ELF、完整Env/Health/Cmd/HostConfig精确多重集合以及其他18实例恢复并有独立receipt。修正只将Binds按完整字符串排序，保留全部内容/重复数和其他严格检查。

第二次OFF成功创建4自有upload并完成154已有begin重试，index154（第155个）UID519801/seq142在10:43:32.598683发出、32.600535收到file_service_unavailable：FileService endpoint is unavailable。这是endpoint_provider.Resolve(kFile)无实例，尚未创建stub/进入FileRPC/SQL。优化flag0，失败不是新snapshotON。30warm+124成功测量、最后失败和全timeline仍保留，ON/B2/A2/完整交互链未执行，不能用部分成功宣称ABBA性能改善。

ZooKeeper明确日志旧0x100051254b700be在10:43:32.568过期；新0x100051254b700bf在10:43:23.850连接，但FileService ready zookeeper_registered=1直到32.686224。新RPC在注册成功前监听，原nc仅证明端口；10:43:30.4/30.6的Get探针成功可能仍通过旧相同target节点。旧node过期与新node创建之间的失败时序和源码一致，Gateway历史完整快照未记录，不能宣称直接测到了每次watch更新。原File已再次恢复，其他18/原配置持久化保持。

## 对准确原因的修正与下一验证

已审计的五RPC97项namedready与前置信号mask修正源码，此前只实际Group验证，File运行没有包含它。现在绑定当前150PASS adapter与原已验证FileMain/FileServer/probe对象，其他全部cached输入与原运行f1b9一致且原宏；重跑97PASS，v2 imagea45522...、ELF064835e92f5abcad832a44ff99e701897c411079105fbca9ae326fc99ec2b980，probeee066590...，UID1000/noNet加载及负例PASS。运行尚未选用。namedready health/信号修正固定在OFF和ON同ELF，只有snapshotflag变化；每次先验证实际注册完成、双GatewayGet、普通线程信号mask、候选SIGTERM退出0，再测量；原Healthcheck/全Env最终恢复。单副本滚动切换零丢请求尚未证明，不把startupgate当作该保证。

## attempt3真实ABBA已完成

18c78f53控制源码；同File ELF064835e92f5abcad832a44ff99e701897c411079105fbca9ae326fc99ec2b980，snapshot OFF/ON/ON/OFF。namedready/早期信号mask/实际注册日志门控固定在两模式，不作为snapshot收益；仅开关不同。每case4真实actor分布两原Gateway，30warm480测量，再30波四并发120。1920测量、120warm、480并发重试；4×658请求/659文件断言正确，216完整操作148完整链断言正确，含每轮真实1.8MiB prepare/chunk/finalize/partialdownload/resume/字节一致。

| 轮次 | 已有begin均值ms | 中位ms | 480样本P99 ms | 最大ms | 4并发观测上界均值ms | 4并发120样本P99上界ms |
|---|---:|---:|---:|---:|---:|---:|
| A1 off | 6.845720 | 6.835712 | 8.904546 | 10.416270 | 11.433473 | 21.729655 |
| B1 on | 5.220292 | 5.171485 | 7.396021 | 9.905162 | 9.631597 | 20.207930 |
| B2 on | 5.019929 | 4.982181 | 6.824712 | 8.702243 | 10.074137 | 20.492335 |
| A2 off | 6.953903 | 6.897013 | 9.194251 | 12.054497 | 11.965870 | 22.410967 |

开启均值5.019929/5.220292ms，关闭6.845720/6.953903ms。保守用最慢ON和最快OFF减少23.744%。顺序/网络/日志/精确校验及deadline相同；这是有限480closedloop微对照，非10k50k人口/到达率容量证明。4并发数据是一次pump收齐响应后每条采集的观察上界，有额外排队/用户态读取，不把它解释为服务处理P99。各实际完整否定响应/取消/后续SQL身份和状态核对保留。

三个候选部署普通后台线程均屏蔽SIGINT/SIGTERM，恰1合法do_sigtimedwait；候选三次SIGTERM退出均0。原File首退出码143也保留。每次namedready明确SERVING/镜像探针SHA正确/实际注册日志之后才测量；没有再次遇到第155项注册窗口错误，但该有限测试不能保证单实例切换零错误。以前两轮FAIL、日志、4个旧upload和150native6schemas完整保留。

最后恢复原File image38dca、f1b9原ELF、完整原Env/原nc Healthcheck/实际Mounts/HostConfig（Binds多重集合），其他18精确实例和私有配置/原持久化/宿主应用保持。原File新CID和时间以.local/codex/file-begin-snapshot-control-20261006-attempt3/restore-summary.json为准。新候选尚未选入长期运行或万人混合；高并发真实分块/终结/恢复、故障/跨功能20k–50k容量仍OPEN。
