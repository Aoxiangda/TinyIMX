# 群规模瓶颈与有界投递迭代（2026-10-06）

全部功能/10k–50k极致性能目标继续OPEN。原100ms门槛/3s期限保持。5c2645/e6873156双Gateway commitwake=1，固定100自有客户端分布两Gateway，正常API创建Group57/58/59/60分别2/16/65/100人，13消息/群（3warm10测量）：52消息/2327收件逐项wire身份/内容/MID/SQLstate3正确、0重复。群和历史保留供相同夹具对照；原19实例/配置/持久化1/1/1/0/0保持。10样本不估总体P99，不是万人容量。源数据 .local/codex/group-fanout-size-analysis-20261006，确认SQLpoll观察上界含docker exec，不是精确ACK提交延迟。

|成员|收件/消息|senderACK mean/max ms|首收件 mean/max ms|全收件 mean/max ms|全收件最大≤100ms|
|---|---|---|---|---|---|
|2|1|20.062 / 23.421|32.319 / 38.675|32.319 / 38.675|PASS|
|16|15|19.882 / 24.378|45.613 / 52.001|133.498 / 160.643|FAIL|
|65|64|29.055 / 36.019|92.395 / 104.635|482.334 / 534.799|FAIL|
|100|99|33.734 / 47.793|99.939 / 117.357|767.800 / 863.790|FAIL|

协调器代码明确逐人dispatch后同步completeRPC再下一个；claim仍逐行UPDATE/同事务COMMIT，peer/receiverACK仍MID排序。不能将曲线全部归因某一步。

候选strict TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE=1只改已提交claim同批（≤claim.limit≤256）dispatch先全部按原顺序提交，再执行完全相同的completionRPC。默认OFF/非1原顺序；异常超limit保留原行为不扩大buffer。无新worker/跨批重排/路由缓存/SQL修改/class布局变动。5000mslease/token、3000mssubmittedretry、1000msfailureretry及原RPC2500ms保持。ACK先state3时原SQLlease+status1fence让completion affectedRows0不可回退；uncertain完成仍记录/原租约恢复，不丢后续work。

TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE=1为每批MID区间/数量/claimRPC/dispatchsum+max/completesum+max/wholebatch/threadCPU数值≤8/s/process，默认OFF不读clock。相同ELF OFF/ON/ON/OFF两模式相同诊断，同四Group100连接，不放宽期限、失败保留。

新增6native含原2租约检查覆盖混合结果/无效work/lease/budget/uncertain completion/ACKwinner affectedRows0/claim失败/oversizedfallback，另保留旧6wake检查，各OFF/ON/invalid。尚未运行，不能标候选PASS/接受。

## 构建实际通过与下一对照

70461a73源码已Git提交。36native（6wake+6pipeline × OFF/ON/invalid）全部PASS；隔离编译仅coordinatorTU，复用接受的原GatewayServer/main/cache及其精确静态链接顺序。候选image bfe65e732629f9620bfa906eb3f7abffdc987a7286eb899581af1358135daa95，GatewayELF4ba5d0ce8f2ceac90ecde77d811d4c61e0b948e2f4fd22349aba8700b0408fc7，loaderPASS/missingconfig原exit1PASS。19运行/原库/私有配置保持，尚未部署接受。

准备同Group57/58/59/60，OFF/ON/ON/OFF4case，每size13msgs(3warm10measured)，总208msgs/9308实际recipient confirmations，另216操作148断言原全功能链。仅2Gateway候选/fullEnv只增deferflag及两模式均trace1，commitwake1保留；最后恢复5c2645及原完整Env，其他17实例不变。低样本规模观察不假称全功能容量。
