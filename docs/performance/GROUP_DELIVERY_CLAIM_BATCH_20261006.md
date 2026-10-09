# 群投递租约领取 SQL 批量候选（2026-10-06）

当前状态：207项真实SQL验证及四轮实际交付/全功能链已完成；65/100人性能仍FAIL，原accepted网关和Message镜像及完整Env已恢复。历史准备段按原样保留，实际结果以本节与下文为准。

按收件人排序及同批先派发后完成降低排队，但真实四轮 65/100 人群仍超 100ms；64 收件人领取 RPC 约 60 多毫秒。源代码仍逐条 UPDATE 租约，SELECT、64 UPDATE、COMMIT 后才派发。本迭代只减少这些已锁定记录的 UPDATE 往返。

默认关闭开关 TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE=1，严格只接受 1。在线和离线领取均使用原 SELECT FOR UPDATE SKIP LOCKED、排序、过滤、上限256、类型验证；仅将已锁定 PK 合并为一次 UPDATE，要求 affected_rows 等于记录数。保留 PING、原 COMMIT、租约、重试和 ACK 赢得状态不被完成 RPC 回退。单批 NOW(3) 时间一致，事务提交前不返回工作。

待执行真实隔离 MySQL OFF/ON/非法开关 pool1/4：精确行集合/排序/空批/256上限/uint64/引用反斜杠/在线租约/离线原状态/并发不重复/SKIP LOCKED；SQL故障、affected_rows不匹配、提交前故障回滚；真实提交后模拟返回错误不派发和租约回收。wrapper仅测试二进制，提交后错误为返回值模拟，并非实际网络断开。全部在六个事先不存在的自有 schema，不复制或修改生产数据。

所有失败、构建中间产物、原记录与测试 schema 保留。同 ELF 四轮真实消息、确认、功能链和 10k–50k 所有功能尚需验收，不得以 SQL 指令减少替代整链路验收。

## 实际隔离测试与部署前修正

207 项真实隔离 MySQL 检查全部 PASS：OFF/ON/非法值01分别pool1的33项、pool4的36项；六个事先不存在自有schema全部保留。每64条领取UPDATE实测64→1；含ACK赢、SQL故障/affected mismatch/提交前回滚、真实提交后模拟错误不返回工作及原租约回收。该模拟不是实际网络断连，8个批次计时也不是人口P99或线上性能验收。

Message候选image89f74b5152c66ea5d93189f11e39415d2cb38bdde178b2feb31e894c577f2490，ELF30574112f4fba576bb118d94b6f1818e4dfa85c691caa19b2b2650966d48f1c1。只重新编译MessageRepository，原accepted主程序/其他objects/宏日志语义保留，无testwrapper。stopped loader及missingconfig1检查通过，原19实例/配置/持久化不变，尚未部署。

第一次控制助手运行在服务身份前置查找抛StopIteration：使用了猜测的message键，实际Compose/docker为message-service。此时新控制stage尚未创建、无部署或数据变更；19原实例不变。保留原工具和本次错误审计，核验真实键后仅修正工具并完整重跑原本未开始的四轮。性能失败不会被隐藏、重试或放宽门槛。

## 同 ELF 四轮真实结果

Message ELF30574112…、固定Gateway ELFc246657a…，只切换Message claim批量OFF/ON/ON/OFF；Gateway recipient/defer1和≤8/s数字trace固定。每case相同Group57/58/59/60、100自有真实连接，13条/size/3warm10测量，原3s期限及100ms门槛不变。

|case|size|ACK mean ms|first mean ms|ALL mean ms|ALL max ms|100ms sample gate|
|---|---:|---:|---:|---:|---:|---|
|A1 off|2|29.718300|48.262033|48.262033|54.832499|PASS|
|A1 off|16|32.369301|64.743782|97.879653|112.471492|FAIL|
|A1 off|65|48.085875|152.175071|284.223534|319.409465|FAIL|
|A1 off|100|52.804411|146.536737|627.330109|674.057386|FAIL|
|B1 on|2|24.588420|40.539034|40.539034|45.754232|PASS|
|B1 on|16|31.292779|49.293458|83.416563|113.177266|FAIL|
|B1 on|65|42.113569|75.193228|237.915335|315.082513|FAIL|
|B1 on|100|49.541201|83.106628|453.840440|591.964776|FAIL|
|B2 on|2|25.125215|42.273342|42.273342|45.754750|PASS|
|B2 on|16|29.017181|45.385959|79.324277|94.992510|PASS|
|B2 on|65|42.289720|80.698861|215.674629|262.871893|FAIL|
|B2 on|100|51.436802|88.297377|484.238215|606.281929|FAIL|
|A2 off|2|24.815793|41.136495|41.136495|45.451663|PASS|
|A2 off|16|31.310262|61.476486|96.816816|102.280008|FAIL|
|A2 off|65|44.700163|140.760265|375.513929|1117.585340|FAIL|
|A2 off|100|57.102758|149.662503|595.409675|683.049130|FAIL|

208条群消息、9308逐收件wire/SQL状态3/attempts/content/identity正确、0重复；216公开操作148断言正确，包含真实1.8MiB文件完整性、好友/私聊/会话/群权限及退出/踢出/解散交叉验证。B1的16人sample max113.177ms也FAIL，不能只挑B2的94.993ms。65/100全收件仍不合格，未选择新镜像常驻/未跑新候选万人混合。10条不是总体P99，SQL轮询确认时间是观测上界不是最后ACK提交时刻。

## 阶段与最慢MID复盘

低频按case同64条batch均值（互斥coordinator阶段，不能视作群端到端P99）：

|case|batch samples|claim ms|dispatch sum ms|completion RPC sum ms|whole ms|thread CPU ms|
|---|---:|---:|---:|---:|---:|---:|
|A1|14|94.206571|74.518643|306.645500|476.808786|53.493857|
|B1|14|33.370357|67.149500|328.176714|429.968857|51.139857|
|B2|14|36.082143|69.026786|347.782286|454.182643|50.708357|
|A2|15|92.784067|68.102467|314.505733|476.711200|52.183800|

claim阶段OFF94.207/92.784→ON33.370/36.082ms；原逐条UPDATE往返下降证据明确，但completion串行328.177/347.782ms和dispatch67.150/69.027ms仍在，100人跨64批次前依然等待前批completion。RPC total内包含Get/Confirm，不重复累加；抽样混合4群/温身/人口不同，不用它推总体均值或P99。peer confirm=-1是未执行，不能显示为负延迟。ACK队列和RPC未获得稳定全面改善，也记录而不隐藏。

A2最慢MID1304、65人：ACK41.622418ms，first93.968101ms，ALL1117.585340ms。49人于201.950879ms前到达，随后877.314857ms空档，余15人自1079.265736ms到达。所有attempt_count=1。晚批coordinator gateway-b领取15条，claim30.409/dispatch19.358/completion67.910/whole117.980ms；peer519801约一秒后queueage仅0.218/get4.103ms，不支持将这次一秒延迟解释为其peer业务排队。未记录每次空claim，不能声称完整证明每个锁的时间线。

源码Run仅processed>=64才继续领取；部分claim即等待恢复1000ms。SKIP LOCKED下49条不说明全体已被领取，15晚批时序与此缺口吻合。下一候选严格defaultOFF且仅commitwake路径，把非空部分批也纳入每轮最多4批/25ms让出规则；保持原SQL租约和所有completion，原空队列/失败退回1000ms，不增线程/无界队列。先原生重现49+15，再实际同ELF对照。该修正只针对部分批等待，不能直接声称消除100人大群completion瓶颈。

source96279c0、buildhelper06af187、control200fe09、identity修正26b8094完整保存。第一控制在任何stage创建和部署之前因实际service key定位错误StopIteration；对照未开始，原19实例不变，随后实际keymessage-service修正已记录，不属于重复丢弃性能失败。两个本地JS准备错误也均发生在执行/写入前，记录在local audits，已修正引用。

## 精确恢复身份

恢复 receipt status ACCEPTED_GATEWAY_AND_MESSAGE_FULL_ENV_RESTORED。完整配置SHA及MySQL1/1/1/0/0不变，其他16实例一致，ownclients closed、hostapps preserved。候选flags均不留在原运行实例。

- /tinyimx-m21-gateway-a-1 CID 34b0345a2e0c667bed5ec7238d5681ac43241508ceb9e3ef0308741f1e6e1f6c; image sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405; start 2026-10-06T09:55:54.121005898Z
- /tinyimx-m21-gateway-b-1 CID d91304d0bf8d7f9adf92bd5fbfb9f4ede533c200d87d3617b87bc321875ff673; image sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405; start 2026-10-06T09:55:54.602033543Z
- /tinyimx-m21-message-service-1 CID 7e7c6ceb86128ded25af572d4fab0af6b7997b9c4d5942ab486477a7b4b5373c; image sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060; start 2026-10-06T09:55:48.595574749Z
