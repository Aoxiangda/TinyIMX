# 群查询 RPC 同调用边界诊断（2026-10-07）

状态：审计后准备诊断，尚未实测或接受性能收益。当前100人组合ALL mean134–135ms、max172–197ms仍FAIL；普通durable SELECT窗口约0.57–0.59ms，而peer Get约7–9ms、ACK Get9–10ms，有尚未分解的等待，不能直接认定网络/数据库唯一根因。

仅改GetGroupMessageDelivery客户端、服务端、FindGroupMessageDelivery函数边界和新独立计时辅助，不改变existing class header/layout、Proto/auth/lease/FIFO/ACK/durable/return/query/deadline，原源完整前镜像保留。默认ENV TINYIMX_GROUP_GET_BOUNDARY_TRACE_UID未配置为OFF，不运行时钟/Hash/TID/日志；精确正uint64选择一个自有收件人519862。数值日志使用MID/UID/request_id FNV64/caller_instance FNV64作同调用关联，不记录rawID/正文/凭据；不是安全Hash，分析需检查键唯一/未配对并明确不能当总体P99。各side日志max8/s独立限频，可能未配对，完整性不足不能作为归因证据。

side1标记Resolve后m1/Stub后m2/Grpc前m3/Grpc后m4/Decode验证响应后m5；side2标记Validate后m1/Application后m2/Fill后m3，构造在参数校验后故不包含校验前耗时；side3标记Acquire（含现有PING）后m1/SQL构造后m2/Query后m3/Decode后m4。同步TLS RAII继承同handler identity，嵌套/早退/exception恢复，无异步跨线程继承。Repo total含局部资源释放，handler total不含gRPC准入与返回后transport；记录total在日志输出前，诊断日志自身成本会进入RPC残差，必须同镜像OFF/ON对照评估扰动，不能把残差都叫network时间。使用同VM/kernel steady clocks并核对time namespace，负时间/重复键/缺失阶段单列，不能静默丢弃。

计划新native测试默认零时钟/zero sink、uint64严格解析、FNV已知向量、Marks/CPU/失败、MID/UID隔离、嵌套关闭与exception、TLS并发、rate8/s并发严格上限；既有业务/SQL/协议测试同SHA继承，原生native不连接网络/SQL、不写数据。构建仅重编三个TU，新Gateway RPCarchive替换唯一MessageRpcClient成员，其余member逐字节核对；Message仅替换Repo和Impl直链对象，旧main/class header/proto exact保存。新独立镜像不部署至验收配置。微诊断控制只比较traceUID0/519862/519862/0，同candidateimages/batch128/coalescing1/clientdeferred1/其余完整Env；四54op37assert链及528消息23628实际收件SQL3，原3秒ACK/ALL、SQL8秒观察保持，未配对/慢/错误保留。finally精确恢复原5c2645/be8及全配置、other16/私有配置/宿主应用。没有全部功能极致/10k50k容量已完成声明。


## 构建校验失败与修正

c331d30下三个产品对象编译与两运行二进制链接、五case合计145项原生验证和原Message application unit全部PASS；镜像封装前过严地要求Gateway也保留GroupGetBoundaryTrace::current_，编译器移除客户端未使用的TLS变量导致断言失败。这个符号仅服务端scope/repository需共享，不能用其存在来判定客户端计时是否进入二进制。原服务完整未变，失败阶段/原日志/对象保留。

新attempt2只修正符号校验：两个二进制都检查数值日志marker且无wrappers，Message再检查TLS符号；客户端是否实际开启由145 native与同镜像真实开关控制验证。所有原源码/依赖/编译链接记录和RPC其他archive成员哈希核对后复用原通过结果与构建对象，不重复编译/测试，不冒称新native运行。控制器指向attempt2成功receipt。8文件源码提交中实际3产品TU+5新增文件，先前apply audit的sixnewfiles文字是笔误，以白名单和Git清单为准。


## 首次控制在部署前停止：时钟域校验修正

初次控制line100将time namespace inode相同当作跨进程单调时间可比的必要条件，Docker的三个容器time namespace编号不同而断言失败。发生在deployment/actors前；只写新控制目录和只读预检，原19实例与上一恢复记录逐个一致，没有开始压力或改变服务。只读review_group_get_time_namespaces记录证明同kernel boot_id、MONOTONIC与BOOTTIME offsets均0。不同namespace编号不代表不同单调偏移。

attempt2在每个case启动/原配置恢复后保存并核对实际boot_id与MONOTONICoffset为0，保留namespace编号供复盘；如果偏移/boot不同则停止，不静默比较跨时钟时间。原失败控制与审计仍保留，新控制freshstage/新CIDggb2，不覆盖原数据。边界镜像仍是884908e下attempt2同二进制，无需重复145native和应用unit。新receipt/source只改变控制与复盘，产品源码/镜像不改。
