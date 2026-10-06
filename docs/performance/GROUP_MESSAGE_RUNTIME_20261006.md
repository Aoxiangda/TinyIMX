# 群消息现有执行器隔离候选（2026-10-06）

状态：源码准备，未构建/运行/验收。前一轮路由批约60ms→4ms后100人ALL仍301–320ms；第二次partial单变量无稳定平均收益。真实限频ACK样本入队至开始99–103ms，Get+Confirm执行19–20ms。代码核验Group peer/ACK使用business_executor_，生产config只有4worker/64stripe/512pending；私聊早已有独立message_executor默认8worker且有界512pending/64stripe，线程总数已经存在。

strict defaultOFF TINYIMX_GROUP_DELIVERY_MESSAGE_RUNTIME_ENABLE=1，只把HandleGroupMessageDeliveryAck和HandleGatewayForwardGroupMessageRequest两个入口同时选择现有message_executor。消息执行器未安装时保留原control fallback；已安装而draining/stopped时拒绝并走原恢复，不切换排序域。两入口继续原GroupDeliveryOrderingKey(mid,authenticated UID)/MustRun/epoch/cancel/submissionreject/队列上限/剩余budget。Execute方法字节完全保持：peer lease授权/完整持久化Get/新鲜本地TCP/去重/Tracker-before-send，ACK durableGet/已登记seq evidence/Confirm UPDATE status<>3/不确定结果处理均不变。

不新增线程、不改worker数/公平调度/CPU affinity/池大小/主机应用/持久化/期限/租约/恢复。Gateway class layout及virtual不变，只重新编译GatewayServer.cpp；已封闭route/claim/completion/API等对象须精确SHA证明，不复用不兼容Main。定时器直接线程安全tracker重试、原replay专用执行器、coordinator已提前Stop、原message executor先于Gateway依赖销毁drain均保持。群send/auth/治理仍原控制入口，后续若有热点需单独定位。

原生计划：strict OFF/ON/非法01/0；固定两个现有1worker执行器，阻塞群peer后原ACK同identity FIFO，ON下profile控制功能独立前进；private与group在同message域的竞争明确验证。原boundedhotkey拒绝/BeginDrain/stopped拒绝无fallback/生命周期计数和原27 tracker全套保持。原14executor/recipientFIFO回归与封闭镜像检查后实际相同image仅runtime旗标ABBA；route/completion/claim/partial/recipient/defer/commitwake固定。需要每轮真实所有收件/SQL3及完整公开功能交叉、再万人混合私聊/列表/群确认共同验证，不把隔离原生检查等同容量。

全部结果/问题/代码Git/预映像保存；当前全功能目标仍OPEN，2万混合FAIL和5万/文件容量/离线故障/AI原配置和CPU推理待完成。
