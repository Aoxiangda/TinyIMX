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

## 原候选真实ABBA结果（全部保存）

49c4ca33控制208群消息/9308逻辑收件逐项wire+SQLstate3正确、0重复；216操作148断言全功能链正确；2Gateway最后恢复5c2645原完整Env（defer/trace均无），其他17实例/配置/durability保持。

|case|defer|2/16/65/100成员全收件mean ms|
|---|---|---|
|A1|off|28.929 / 129.987 / 508.262 / 788.122|
|B1|on|28.566 / 85.315 / 244.309 / 536.071|
|B2|on|32.661 / 90.394 / 273.236 / 559.205|
|A2|off|32.591 / 148.668 / 481.102 / 774.455|

两ON65成员mean244.309/273.236ms vsOFF508.262/481.102，100成员536.071/559.205 vs788.122/774.455；有稳定方向收益但16/65/100仍至少一次>100ms，不接受为极致/总体P99/万人容量。发送ACK/首收件重叠，不宣称这些得到因果改善。全部逐消息、逐收件、每size10测量及3warm retained。

64收件批次阶段抽样（含warm，两规模26批/case或预算采样更少），exclusive协调器阶段不是端到端人口P99；A1claim63.076ms/dispatch54.787ms/complete369.292ms/whole488.257ms。完整按case数值：
{
  "A1": {
    "samples": 26,
    "mean_ms": {
      "claim_rpc_us": 63.0761923076923,
      "dispatch_sum_us": 54.7865,
      "complete_rpc_sum_us": 369.29173076923075,
      "total_us": 488.25711538461536,
      "thread_cpu_us": 50.18934615384616
    }
  },
  "B1": {
    "samples": 26,
    "mean_ms": {
      "claim_rpc_us": 64.80053846153847,
      "dispatch_sum_us": 45.662615384615385,
      "complete_rpc_sum_us": 286.4925769230769,
      "total_us": 397.935,
      "thread_cpu_us": 41.9451923076923
    }
  },
  "B2": {
    "samples": 24,
    "mean_ms": {
      "claim_rpc_us": 63.892916666666665,
      "dispatch_sum_us": 47.379041666666666,
      "complete_rpc_sum_us": 297.1609166666667,
      "total_us": 409.497125,
      "thread_cpu_us": 44.593666666666664
    }
  },
  "A2": {
    "samples": 26,
    "mean_ms": {
      "claim_rpc_us": 63.630307692307696,
      "dispatch_sum_us": 53.636346153846155,
      "complete_rpc_sum_us": 358.88192307692304,
      "total_us": 477.36565384615386,
      "thread_cpu_us": 48.079807692307696
    }
  }
}

默认64batch下100成员第二批必须等第一批complete，所以延期同批complete不是跨批完整消除等待。receiver-side peer/ACK同MID有真实代码串行证据，但本轮尚无queueage计时，不能唯一断言全部剩余延迟由它造成。

## 下一recipient排序候选

Strict TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE=1令group peerreceive和receiverACK按相同GroupDeliveryIdentity(MID,recipient)的原hash排序；OFF/非1仍MID原key。ACK recipient来自已鉴权session。相同收件的重复peer/ACK保持顺序，不同收件允许不同stripe；队列/worker数/限流/取消期限/epoch fence/peerlease验证/durable Get/Confirm/tracker mutex均保留，私聊/群写操作key不改。

共享max8/s/process数值诊断补充peer(kind1)/ACK(kind2) dispatch_age/work/getRPC/confirmRPC/threadCPU，将排队与工作耗时分开，OFF无clock。原生测试实际hold一个deliverytask，检查同identity重复仍等待，不同recipient ON可独立运行，另完整原14executor tests。候选尚未构建/实测，不假标PASS。

## recipient构建首次检查FAIL及修正审计

b2b1fd30的新Gateway/coordinator编译和42native（旧36+recipient6）全部PASS。完整executor14检查中13PASS，唯一FAIL是历史fairhandoff顺序；无runtime部署/配置改动/数据库写。本任务build-20261006/failed.json和original-executor-native.log保留。

准确核对当前executor源码SHA3b707e2e5a63cd7671a92c977ea75fc7fcaad29de1c90e92e41bcb8f9017cb22，原while-drain队列至空；ROOT_CAUSE_REVIEW此前已记录公平候选性能FAIL/回退及该源码恢复。测试faircase却遗留期望1,0,2,3,4,5，原实现实际1,2,3,4,5,0。这不是recipient身份FIFO错误，也不允许绕过其他13失败。

修正只更新该fixture的显式调度契约：默认验证当前原drain顺序/同stripeFIFO/全部6task计数；保留 --expect-fair-handoff 模式验证原公平顺序，原executor应仅该项FAIL。全部14默认必须PASS；历史模式exact1FAIL必须保留。executor源码/库不变，不重新接受拒绝过的fair方案。attempt2复用本任务已经成功且源/flags逐SHA验证的两个TU对象，重新编译测试；先检查56native再封新镜像，仍未性能接受。

## 对象复用审计attempt2前置FAIL

attempt2在本任务已编译对象的dependency guard停止、尚未重跑native/封镜像/部署。复用对象.d真实记录第一次构建的originalmacro overlay，guard误要求attempt2新overlay路径而FAIL。attempt3只修正助手，验证旧overlay SHA仍原macro1d4e7adc…，保留实际编译路径、对象/.d均不重写；整个先前失败stage保留。不是业务超时/数据正确性失败，不修改产品或放宽native检查。
