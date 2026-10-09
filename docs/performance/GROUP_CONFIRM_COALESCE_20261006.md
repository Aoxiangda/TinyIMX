# 群收件确认有界合并候选（2026-10-06）

状态：源码和验证用例准备，尚未编译/运行/实际验收。窗口诊断1bcda00：100人每33消息3267独立确认UPDATE，4worker SQL mean6.062ms、8worker11.160ms；更并发时池有少量0槽及slot1.127ms，但不能解释所有RPC等待。既有route/completion已减少fanout批阶段，原单收件确认仍逐个autocommit。实际SQL/行锁/redo/binlog证明具体成本，未认定唯一原因或直接扩大池。

严格默认OFF TINYIMX_GROUP_CONFIRM_COALESCE_ENABLE=1，只改MessageRepository原ConfirmGroupMessageDelivery进入有界caller-led合并；原single-statement方法字节保持。Gateway durableGet在Tracker registered attempt seq之前，原Confirm RPC、认证UID、deadline、协议/application/adapter、TCP/重试完全保持。Repo仅新增非virtual方法，不增加成员/构造/虚表或ABI布局；perrepository弱注册表由活跃RPC临时shared_ptr持有，完成全部调用后无后台线程/状态拥有者，不合并不同repository，不保留pool/data指针异步执行。

每批最多64、待处理256+运行64，空闲首caller收集最多500us；执行原RPC调用线程，FIFO入队，超限StorageError且不触发SQL，不静默回退另一提交路径。批callback异常/返回数不符只唤醒失败，不自动重放可能发生的副作用；失败结果入队前分配，返回move不会抛出。现有RPC server graceful stop/Wait管理调用生命周期，未新增后台shutdown依赖。

一人批保持原Acquire健康PING+原autocommit UPDATE。多人批一次Acquire/PING，BEGIN、按排序复合主键锁定SELECT、原UPDATE SET/WHERE status<>3、COMMIT；锁下精确计算每人original 0/1 affected_rows，重复caller按FIFO第一个1其余0，已delivered/缺失0。每个RPC在同持久提交确认后才成功；COALESCE(delivered_at,NOW(3))、清lease/error、lastgateway/attempt/retry字段保持。不会把总affected_rows发给每个请求。invalid/65请求在Acquire前拒绝；uint64保持整数SQL字符串。批次同一commit时间对应逻辑线性化，不承诺与原单条SQL毫秒timestamp逐字相同。

新增<=8/s数值阶段诊断默认OFF TINYIMX_GROUP_CONFIRM_BATCH_TRACE_ENABLE=1，保存requests/unique/affected/acquire/begin/query/update/commit/total，不记录内容、身份或租约密钥。确定UPDATE error原事务rollback；COMMIT结果不确定所有参与callerStorageError，不自动回放，cleanup失败Close隔离；原后续确认可显式观察已提交状态3并返回0。合并增加事务错误影响的请求数，必须保存失败耦合。

原生验证：caller-led有界/FIFO/overload/异常无重发/错误唤醒/生命周期；真实新建本任务schema、pool1/4、OFF/ON/非法01/0，percaller状态0/1/duplicate/missing/metadata/64/65/整数边界/SQL与触发器原子错误/真实COMMIT后synthetic错误/并发32caller及重复/ACK与completion竞态/关池。SQL形状EXPLAIN必须主键。所有wrapper只在native ELF，运行镜像禁止wrapper；完整封闭旧Message链接只替换Repo TU并保留其他字节，main无需布局变更。先native/旧应用/协议回归、同ELF单变量入口ABBA与全功能链，再10k/20k交叉；组件32并发不代表容量或整体P99。

全部功能目标仍OPEN，20k混合FAIL、50k全部功能、文件并发真实传输/离线恢复/故障稳态/AI原profile及CPU推理待解决。候选未选入运行。全部预映像、代码、命令、失败、原值和restore需要Git保存，不删除其他任务文件/缓存/主机应用。


## 原生、真实 SQL 与旧 RPC 回归通过，入口对照待运行

源码57c48ff；构建助手7b16e7e/3ab2d62/da26cdf。49有界caller原生+6模式46各=276真实SQL+原completion协议62真实gRPC/MySQL共387检查PASS，原Message应用回归PASS。6模式OFF/ON pool1/4和strict非法01/0；每人0/1结果、missing/repeat、精确uint64、状态与metadata、64/65、SQL/触发器原子rollback、实际COMMIT后fakeAPI错误没有自动重发、32同时确认与重复、12batchACK/completion竞态、关池及主键range/rows2均验证。没有改变schema防护、期限、持久化或运行服务。

32同时scalar confirmations每组件6样本，OFF均32UPDATE/32PING，ON全为2UPDATE/2PING/2显式COMMIT；pool1 mean157.208→36.939ms，pool4 53.815→47.821ms。仅独立组件6样本，不估P99、不当作入口收益或万人容量。多连接MySQL原group commit已经帮助OFF，故不能从减少命令数宣称必然巨大实际收益。

两助手失败完整保留：attempt1在完成325check及原应用后RPC编译缺common.pb.h，改用tinyimx_message_grpc目标与common generated路径；attempt2编译/链接成功后原RPC fixture拒绝非codex_group_complete_20261006_rpc_前缀，拒绝发生在fixture写行之前，已有本任务空schema保留。attempt3用原严格前缀的新absent schema，不改测试限制；复用精确原对象、原始测试结果/SHA，非重复运行声明。不是候选产品故障，也未影响19服务。新fixture创建前加入明确原前缀校验，防止再次错误方向。

封闭Message仅替换Repo TU；class fields/ctor/virtual/proto/API旧路径不变，原single Confirm body完全保持。其他所有封闭链接输入SHA不变、运行ELF无mysql wrapper；UID1000/network none/read-only/capdrop ldd-r、缺配置expected exit1通过。image tinyimx/runtime:codex-group-confirm-coalesce-message-v1-20261006，645e9cc183f42804933f660aded2a926d8ea8f16e32eb9119c98b5d3bd74a8ee，ELF08042d14e52373fc09c06f42b06d6d7e5ac6729a9811de9b19519cc6e7bb641f。387checks封闭build receipt group-confirm-coalesce-build-20261006-attempt3；19/私有配置/持久化和主机应用全部保持，未部署。

下一同Gateway818bea35/066acab3和同Message645e9cc/08042，仅Message coalesce0/1/1/0；原Gateway message-runtime固定1、partial/route/completion/claim/recipient/defer/commitwake全部1，各messageRPC健康就绪固定、原3秒ACK/ALL及SQL8秒观察截止保持，四组33消息每case及四完整54op37assert链。保留限频coalescing阶段与每size只读SQL计数窗口，不给30样本估P99。完成后精确恢复原5c2645/be8全Env/Health/HostConfig/Mounts/其他16，所有失败保留。全功能万人混合/20kFAIL/50k全部feature及AI仍OPEN。


## 真实ABBA完成：减少SQL但没有稳定端到端改善（最新结论）

控制5f05f4c，group-confirm-coalesce-control-20261006；同818bea35/066acab34 Gateway和645e9cc/08042d14 Message，message-runtime固定1（实际16worker），partial/route/completion/claim/recipient/defer/commitwake固定1，仅确认coalesce0/1/1/0；trace两模式相同。每size33消息/3warm30测量，528消息/23628实际wire及最终SQL状态3、216完整链操作/148断言、0观察重复，原3秒ACK/ALL与SQL8秒观察期限保持。

|case|2人ALL mean/max ms|16人|65人|100人|
|---|---:|---:|---:|---:|
|A1 off|37.557/44.252|61.193/111.798|130.368/196.95|261.2/388.006|
|B1 on|37.712/45.183|58.237/72.692|119.478/149.103|247.59/299.913|
|B2 on|38.059/44.296|53.835/71.487|104.878/142.381|223.36/275.073|
|A2 off|33.463/44.501|55.364/73.773|114.126/153.686|241.108/299.601|

全部30样本不估P99。最慢ON对最快OFF均值收益2/16/65/100分别-13.735/-5.190/-4.690/-2.688%，没有稳定端到端收益；65/100均FAIL，A1的16人max111.798ms失败保留。不能挑B2最快轮或SQL减少宣布突破。

100人33消息窗口OFF3265/3267次确认UPDATE，ON57scalar+324batch=381、54scalar+336batch=390，约减少88%；并行累计确认语句时间OFF34.727/32.517秒、ON1.914/1.753秒。Get仍4915/4916次。全局P_S非原子且语句事件完成边界不同，affected/count小差保存，不认定丢消息或精确fsync映射，逐人wire+SQL3核对为准。确认阶段偏置样本ON每批平均约7.5–8.2caller，阶段总平均12.8–15.4ms；ACK Confirm RPC均值仍33–39ms。SQL减少不能替代排队/同步RPC/批等待关键路径分析，不把嵌套不同样本均值相加。

analysis.json最初task字段映射不完整，task-analysis-v2.json用dispatch_age_us/get_rpc_us/confirm_rpc_us更正并保留旧版。结果与SHA收据保存于benchmark/local_capacity/results/group_confirm_coalesce_and_measurement_io_20261006.json。restore-summary状态ACCEPTED_GATEWAY_AND_MESSAGE_FULL_ENV_RESTORED，原5c2645/be8映像、全配置、其他16、持久化与宿主应用精确保持。候选默认OFF，尚未进入10k/20k容量验收。



## 2026-10-06最终现状审查：实际线程与测量方法纠正

实际两个Gateway环境TINYIMX_MESSAGE_WORKER_THREADS=16，每个消息执行器16worker；business4worker。前文8是代码默认值，错误地用于运行解释。runtime及SQL诊断实验同映像/完整配置固定，不影响单变量对照，但正确并发解释为4→16。原记录与数字保留，不按错误线程解释重复扩池或加线程。

最新确认合并387验证PASS、实际528消息/23628逐人wire与SQL状态3、216操作/148断言。确认UPDATE约减少88%，100人ALL两ON247.590/223.360ms、OFF261.200/241.108ms；最慢ON对最快OFF没有稳定均值收益，65/100仍FAIL，未部署。原运行完整恢复。完整结果见[当前性能与竞争力评估](CURRENT_PERFORMANCE_ASSESSMENT_20261006.md)和GROUP_CONFIRM_COALESCE_20261006.md文末。

群微测试send后同步重写累计证据，再读响应；同Guest离线重放100人后段约1.62MB写入平均47.310–61.132ms，编码CPU占大部分。99条合成接收记录逐条写平均11.319ms。不能从历史数字直接扣除或改FAIL，必须先移出测量关键路径并校验新旧观察，再定位全链。当前1万指定混合已通过，不等于每个功能高频/5万人容量，2万失败、文件高并发、AI原profile/推理、多端/故障/长期稳态未验收。
