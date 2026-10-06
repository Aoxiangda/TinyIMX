# Message同步RPC轮询profile对照（2026-10-07）

状态：准备可验证候选，尚未测量或接受。上一同调用Get约13ms、handler约2.5–2.7ms，约6.4ms在handler构造前、4ms在handler计时后；这些范围包含准入/传输/调度/诊断，不能认定唯一瓶颈。实际gRPC默认CQ1/min1/max2，而MessageServer未显式设置。TINYIMX_MESSAGE_RPC_POLLERS_ENABLE只精确1开启CQ1/min4/max8，其余不调用SetSyncServerOption，保持原librarydefault；不改existingMessageServer class/API/layout、oldmain/Proto/auth/lease/FIFO/ACK/query/durability/业务worker16或SQLpool16。MIN/MAX是同步轮询线程数，不是RPC总handler上限。可能增加CPU调度开销，需要实测。

native仅自有ephemeral127.0.0.1 fakeMessageService，无SQL/外部数据写入。覆盖absent/invalid/realENV、空service/地址/repeatedStart/readiness、16nativecaller各4次共64RPC身份内容一致、错误状态、Wait/Shutdown/重启fence；各case新进程，原native/日志保留。编译仅新Server.o和nativeTU，Message原密封链接替换Server对象，其余borrowed对象/包逐SHA核对，旧class header/meta/编译布局保持。新独立image不直接部署验收配置。

计划同新Gateway ac546和同新Message profile image，只开pollerflag0/1/1/0，固定traceUID519862/batch128/coalescing1/clientdeferred1/完整其他Env。四同自有群/100连接/528消息23628逐人wireSQL3、四54op37断言及文件字节，原3秒ACK/ALL和SQL8秒观察保持，不估P99/容量。分析同调用是否减少handler前等待与端到端ALL，同时观察其他大小/完整串联退化，不以平均或RPC指标改善掩盖tailFAIL。最后完整恢复原配置与19服务/other16/私有配置/宿主应用。全部功能1万到5万极致目标OPEN。
