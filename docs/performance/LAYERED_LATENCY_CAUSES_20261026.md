# 当前差距逐层定位与执行记录

更新：2026-10-26T07:28:36.525657+00:00

实际源码输入及固定peer差异见PERF1437/1441；以下新增批处理漏计路径已真实构建并实测。
每项都给出动作、实测位置、比较结果和下一验证。完整业务207、MCP23及14次真实进程终止恢复通过。

## PERF-1468-C1 客户端本地排队

入队→写入平均0.018488ms/P990.0456ms；计划→入队平均0.307279ms/P990.8985ms。

已执行：原始60000条逐条分开计划→入队与入队→socket写入。

结果：benchmark/results/all-path-batch-phases-r1465-perf1460-p1-wire-users20k/result.json

下一验证：增加时长、用户和速率时继续记录未发/拒绝/超时和这些阶段，不改变发压端掩盖服务器成本。

## PERF-1468-C2 网关会话前驱和恢复调度

一条ACK75.3128ms中网关排队55.050ms，而本条native仅0.531ms。另一条排队36.400ms，其中36.346ms与同会话前驱执行重叠；前驱RPC35.110ms。

已执行：484条当前实际Gateway/RPC/native同MID关联，保留实际会话顺序。

结果：benchmark/results/all-path-batch-phases-r1465-perf1460-p1-wire-users20k/result.json

下一验证：限额1对照已重新启动；后续单独计时RPC完成→业务续体排队→恢复，同时审计同步接收确认占用相同工作线程。

## PERF-1468-C3 批内串行身份与SQL引用登记

本轮3116/60000请求进入多输入批。52个已记录批次总时均值12.6307ms，身份2.1900ms、引用6.2088ms、引擎3.3844ms。最慢已记录3条批共26.891ms，其中引用12.789ms、引擎11.447ms。

已执行：新增批成员MID和准备/锁/引用/引擎阶段计时；9程序真实构建、14次终止恢复、207业务和23MCP通过。

结果：benchmark/results/all-path-batch-phases-r1465-perf1460-p1-wire-users20k/result.json；benchmark/results/peer-20261017/PERF1450-front-preparation-limit-build-and-contracts-r1/result.json

下一验证：同一程序64/1先形成批内对照，再做无采样64/1/1/64；只在结果支持时保留。

## PERF-1468-C4 缓存命中后的持久化写等待

本轮全WAL调用P992.466928ms；已记录暖请求引擎包络最高20.875ms，身份及引用仅数微秒。锁、引擎写队列和同步调用分开。

已执行：固定同步探针58486次实际WAL调用，批/标量各自统计，不把共享批CPU乘以成员数。

结果：benchmark/results/all-path-batch-phases-r1465-perf1460-p1-wire-users20k/result.json

下一验证：核对固定RocksDB11.8.1内置PerfContext的写线程等待/写WAL/写memtable字段，在独立诊断关闭引擎内部等待的剩余缺口；同步持久化保留。

## PERF-1468-C5 持续CPU差距的资源账单

整机5.79058核；MQ Proxy/Broker1.82057、Message1.23178、双Gateway1.21754、projector0.39346、MySQL0.43464、Redis0.26187核。两个最后30秒均约5.60864核，持续开销未随冷登记结束消失。

已执行：对用户引用的两个候选轮次按覆盖秒数加权，单独保留最后30秒CPU。

结果：benchmark/results/user-reference-capacity-and-cold-work-r1459/result.json

下一验证：前台准备与可靠后台分别优化；同负载报告组件CPU与RPC/提交次数的变化。固定peer为同步本地存储和进程内事件路径，Tiny保留额外外部投影和持久接收确认。

## PERF-1468-C6 负载规模与可持续速率

实际计划1000消息/s，20K用户平均3条/分钟；候选末尾在途4/5，最大在途63/62。当前60s未见前台持续扩队；长时间后台和更高人口/速率分开验证。

已执行：四轮240000条按每秒入队/ACK/投递及首次会话分组。

结果：benchmark/results/user-reference-capacity-and-cold-work-r1459/result.json

下一验证：保持业务条件，先20K/1000s的600s，再40K同速率600s，再20K按1000/1500/2000/3000s分级；保存每段前台与后台队列斜率、完整失败分母和CPU/延迟。

## PERF-1475 第二轮维护前保留数量漏更新

这是发压前测试脚本错误，不是消息业务失败；原20已恢复后才重新接续。

已执行：修正旧第二轮3898为3994；新增实际维护断言的2点和4点正反例检查。；复用P1已关闭实际数据，原失败未覆盖，第二轮客户端/namespace此前未启动。

结果：benchmark/results/retention-progression-preflight-r1475/result.json；benchmark/results/peer-20261017/PERF1476-recovered-preparation-phase-followthrough-r1/result.json

下一验证：完成第二轮实际阶段计时、60000条独立持久审计和原20恢复；再启动无采样对照。
