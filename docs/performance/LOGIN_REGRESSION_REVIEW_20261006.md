# 登录退步复核与当前未达标原因（2026-10-06）

本轮是证据复核，没有产品性能突破，没有新增压力测试。所有功能、交互以及10k–50k容量验收仍未达到。

## 同镜像、同发生器的真实登录退步

| 原始轮次 | 成功登录 | 成功登录均值 ms | 成功登录 P99 上界 ms | 登录门禁 |
| --- | ---: | ---: | ---: | --- |
| sqlbatch150B1 | 10000 | 31.735 | 79.9 | 10000完整登录 |
| sqlbatch150B2 | 10000 | 31.624 | 74.9 | 10000完整登录 |
| logging150O1 | 6357 | 335.858 | 2423.9 | 中途超时，未进入正式窗口 |
| authcurrent10kdiag | 9122 | 307.740 | 2407.0 | 中途超时，未进入正式窗口 |

前三轮Gateway a8b7、Message be8、User 38dca完全相同镜像；第四轮仅User为有界数值诊断1ff078，已经恢复38dca。四轮coordinator SHA1ef1c527...与worker SHA5d6bd183...相同，源码固定总登录爬坡100/s、相同自有700001–710000用户、published target/sourceIP、原业务期限。旧私聊150/s是登录完成后才启用的负载，不能解释登录中途失败。历史10000成功与当前6357中止不能只以“指标不同”回避；这是真实的登录表现退步。中止人数不是稳定容量，成功样本分位不包含失败请求，失败轮与完整轮分位仍存在选择差异。

原镜像也失败，否定“已证明新lazy日志代码单独导致回退”的说法；不能据此排除其它代码、状态或环境因素。时间、进程生命周期、缓存和共享主机条件尚未严格匹配。最近组件诊断、构建和工具修复没有新增真实容量收益，不能累计成产品性能优化成果。

## 已确认与尚未确认的瓶颈

authcurrent10kdiag的379个同uid/tid配对认证样本：密码wall平均45.454ms、threadCPU15.163ms，同样本平均差30.291ms；查找wall5.571ms/CPU0.625ms；handler wall52.071ms/CPU16.141ms。采样为成功UID有界子集，不能从客户端P99 2407减handler P99 186得到精确排队。方法不包括gRPC入口前或返回后。gap包含线程未运行的时间，但未唯一分解成运行队列、锁、缺页或虚拟化暂停。

同期19爬坡区间guestCPU busy平均91.598%，最低MemAvailable约8GiB，三个目标服务CFS throttled delta0。高CPU忙碌和回放任务拒绝是实际现象，未证明其唯一因果链。当前内核sched_schedstats=0，现有schedstat不能提供有效runqueue wait；本轮未开启内核统计、未改系统设置。

只读源码复核排除两项未经证实的猜测：FindByUsername局部数据库连接在函数返回时释放，PBKDF2在其后执行，未持有该数据库租约进行密码计算；UserRpcClient缓存mutex保护stub lookup/create，实际Authenticate RPC在锁外执行，不能声称全体认证被此锁串行化。密码仍为原OpenSSL PBKDF2-HMAC-SHA256/100000，未削弱认证。

## 自有测试数据状态核查

详细审计后仅SELECT聚合、EXPLAIN（非ANALYZE）及公开index元数据；19容器身份和所有私有配置SHA前后相同。当前已有10000个准确自有有效用户，当前该范围private pending=0、group deferred=0，两个计数都采用LIMIT10001有界索引读取；0即完整当前计数。它们是测试结束后的状态，不能重建失败发生瞬间，也不能据此否认空查询/回放调度的资源成本。不能将“历史离线积压越来越多”写成已证根因。

私聊pending identity-only EXPLAIN为idx_im_private_messages_to_status_id range/无filesort，群recipient identity-only EXPLAIN为idx_im_group_delivery_recipient ref + message PK eq_ref/无filesort；这不是生产完整投影或FOR UPDATE锁等待的计时证据。information_schema统计估计private895714行/data271384576/index209879040字节、group612行；估计值不是实际行数或增长序列。

首个历史全状态COUNT DISTINCT审计查询在5s服务器执行预算内超时3024，尚未产生完整统计；失败原目录保留。EXPLAIN显示它按pending-recipient索引扫描估计895714行，而真实按用户的pending查询不走同一整段扫描。该审计查询不是产品业务查询，禁止用这次5s超时宣称生产登录SQL卡5s。后续没有延长预算或加索引，而采用EXPLAIN和有界计数，约0.16–0.21s含docker exec整体时间也不是native SQL延迟。

## 当前为什么仍未达到预期

1. 私聊safe SQL7→5调用在先前10k/150消息s控制中将ACK P99从156.7–164.5改善到132.3–135.0ms，但均FAIL，改善幅度没有消除长尾。
2. 当前登录门禁本身失败，后续消息窗口未启动，更不能宣称其它功能高并发全部达标。少量功能链49operation/35assertion通过仅证明所测逻辑，不证明所有功能并发性能。
3. 未完成同一请求从客户端→Gateway排队→认证RPC入口/执行→presence/unread→响应送达的关联证据。此前过多组件方向测试没有足够快收敛到实际关键路径，不能以“持续迭代”代替真实收益。
4. 原镜像之前通过登录、如今失败的问题尚未唯一归因。下一轮必须先使原基线可比较，补齐排队和发生器CPU归属，然后只改证据指向的环节，再以原期限/安全/功能/持久化完整门禁验证。未经验证的阈值放宽、减少功能或删数据都不能当作解决。

证据：.local/codex/login-regression-review-20261006（首查询失败）、login-regression-query-plans-20261006-attempt2（只读计划及有界计数）；Windows evidence/login-regression-review-20261006/historical-login-comparison.json 与 guest-query-plan-summary.json；此前v72压测原始包SHA e0238a58e0b7e0404163149ee4152e45d618fbcaa133969d489b6a478eafa36f，2156文件已逐一hash核验。
