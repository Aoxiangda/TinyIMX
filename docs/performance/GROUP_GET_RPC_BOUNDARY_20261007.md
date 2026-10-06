# 群查询 RPC 同调用边界诊断（2026-10-07）

状态：审计后准备诊断，尚未实测或接受性能收益。当前100人组合ALL mean134–135ms、max172–197ms仍FAIL；普通durable SELECT窗口约0.57–0.59ms，而peer Get约7–9ms、ACK Get9–10ms，有尚未分解的等待，不能直接认定网络/数据库唯一根因。

仅改GetGroupMessageDelivery客户端、服务端、FindGroupMessageDelivery函数边界和新独立计时辅助，不改变existing class header/layout、Proto/auth/lease/FIFO/ACK/durable/return/query/deadline，原源完整前镜像保留。默认ENV TINYIMX_GROUP_GET_BOUNDARY_TRACE_UID未配置为OFF，不运行时钟/Hash/TID/日志；精确正uint64选择一个自有收件人519862。数值日志使用MID/UID/request_id FNV64/caller_instance FNV64作同调用关联，不记录rawID/正文/凭据；不是安全Hash，分析需检查键唯一/未配对并明确不能当总体P99。各side日志max8/s独立限频，可能未配对，完整性不足不能作为归因证据。

side1标记Resolve后m1/Stub后m2/Grpc前m3/Grpc后m4/Decode验证响应后m5；side2标记Validate后m1/Application后m2/Fill后m3，构造在参数校验后故不包含校验前耗时；side3标记Acquire（含现有PING）后m1/SQL构造后m2/Query后m3/Decode后m4。同步TLS RAII继承同handler identity，嵌套/早退/exception恢复，无异步跨线程继承。Repo total含局部资源释放，handler total不含gRPC准入与返回后transport；记录total在日志输出前，诊断日志自身成本会进入RPC残差，必须同镜像OFF/ON对照评估扰动，不能把残差都叫network时间。使用同VM/kernel steady clocks并核对time namespace，负时间/重复键/缺失阶段单列，不能静默丢弃。

计划新native测试默认零时钟/zero sink、uint64严格解析、FNV已知向量、Marks/CPU/失败、MID/UID隔离、嵌套关闭与exception、TLS并发、rate8/s并发严格上限；既有业务/SQL/协议测试同SHA继承，原生native不连接网络/SQL、不写数据。构建仅重编三个TU，新Gateway RPCarchive替换唯一MessageRpcClient成员，其余member逐字节核对；Message仅替换Repo和Impl直链对象，旧main/class header/proto exact保存。新独立镜像不部署至验收配置。微诊断控制只比较traceUID0/519862/519862/0，同candidateimages/batch128/coalescing1/clientdeferred1/其余完整Env；四54op37assert链及528消息23628实际收件SQL3，原3秒ACK/ALL、SQL8秒观察保持，未配对/慢/错误保留。finally精确恢复原5c2645/be8及全配置、other16/私有配置/宿主应用。没有全部功能极致/10k50k容量已完成声明。
