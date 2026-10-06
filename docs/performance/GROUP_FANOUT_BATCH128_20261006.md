# 群fanout有界批次64/128对照（2026-10-06）

状态：源码控制器准备，尚未测量或接受。延后证据ABBA已证明100人仍约218–230ms，不能归因累计JSON。既有每claim64、100人99收件跨批，源码第一批completion同步完成后才开始下一claim；捕获64批领取约23–25ms、路由约4ms、完成29–31ms。第二批peer提交可能因此晚，需单变量验证。

不改产品代码或默认batch64；既有Gateway ENV TINYIMX_GROUP_FANOUT_BATCH_SIZE=64/128/128/64，Message coalescing0、客户延后采集1和其他candidate flags固定，同镜像完整其他配置固定。现有coordinator claim max256、OnlineStatusCache路由max256、completion RPC max256；选择128只为覆盖100人群99收件的一批，不盲目设无限大小。保留FOR UPDATE SKIP LOCKED/事务/lease5000/重试3000/持久化1/1/1/0/0，existingmessage16worker、pool16均不变；大批次可能增加突发和影响其他消息公平，不能凭小样本部署。

四自有群/100实际连接/528新消息/23628逐人wire与SQL3，四完整54op37assert链，真实文件字节/权限/重复/原3秒ACK及ALL/SQL8秒观察保持，所有失败/原数据保留，不估P99。控制完成精确恢复原5c2645/be8、原默认batch64与全配置及其他16；新native采集25项继承同源已通过，非重复运行声明。若有效仍须同版本10k/20k高频全功能交叉/热点/过载/故障/长稳态，目标OPEN。
