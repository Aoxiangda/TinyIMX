# RPC 注册就绪与退出生命周期复盘（2026-10-06）

## 真实失败保持 FAIL
Group JOIN 同 ELF OFF 控制 GetGroup 完成100测量，成员页完成96，第97个在 UTC 06:44:26.574918 发出、26.575495 返回 group_service_unavailable，约0.581305 ms，未进入 Group RPC/SQL。ON及功能链未执行。整轮 ABBA FAIL，不接受性能结论。

ZooKeeper 原始日志旧 session 0x100051254b70060 在06:44:26.566过期；候选新 session 0x100051254b70061 在06:44:19.585连接，但06:44:26.638才报告 zookeeper_registered=1。请求落在注册过渡窗口；原 nc 只检查TCP，server.Start早于registrar.Start。Gateway历史缓存 generation 未保存，不声称直接测量。原Group已恢复，其余18实例未变，持久化1/1/1/0/0。

## 共用缺陷的证据
五服务在 ProcessTelemetry.Initialize 之后才屏蔽信号。第三次只读诊断按 docker top 父子关系和精确cmdline识别真实业务进程：Group27/User33/Message32/File28/Social28个线程，各21个线程未屏蔽SIGTERM。它们不会继承事后主线程屏蔽，进程信号可能绕过sigwait默认终止。历史具体获信号线程不可追溯，保留限制。

第一诊断误读docker-init，只作包装进程记录；第二按comm过滤遇到Linux名称截断而中止；第三精确路径成功。前两份保留，不作业务结论，不改运行。

## 修正与验证状态
五个main最前面屏蔽SIGINT/SIGTERM再初始化所有组件。五个server新增命名tinyimx.ready健康服务：监听先NOT_SERVING，原有依赖/注册成功后SERVING，退出先NOT_SERVING再注销和关闭。命名服务开始未知，不受默认空服务的默认SERVING影响。
新增探针只发送固定健康协议，原有gRPC库/2秒deadline，仅接受明确SERVING，非零失败。
新增可选Compose覆盖，需要镜像同时打包探针及新版服务。原生产Compose未改，以保护旧镜像恢复。参考官方：
https://grpc.io/docs/guides/health-checking/
https://github.com/grpc/grpc-proto/blob/master/grpc/health/v1/health.proto

不改业务SQL/权限/事务ACK、临时节点所有权、快照失效策略、超时或主机VM设置。计划编译五入口和五server，真实原生五server就绪测试；首先只部署Group候选验证注册/退出与四轮跨功能控制。其他四运行镜像需绑定原优化对象后再验证，不声称已部署。动态注册恢复和单副本切换零丢请求仍未验证。本提交状态：待构建及实测，无性能接受。


## 原生测试夹具失败与更正

隔离5入口/5服务器及探针编译通过；首次原生测试空grpc::Service没有同步业务方法，gRPC报 At least one of the completion queues must be frequently polled，Start拒绝。真实业务接口有同步方法；修正夹具为各服务真实生成的Service接口，其方法默认UNIMPLEMENTED，不调用DB或业务应用。首次日志/对象/FAIL保持。新attempt2仅重编测试夹具，冻结并复用已成功的10个入口/服务器对象及探针/群grpc归档，运行实例不变，不把验证夹具失败计为业务端点失败。
