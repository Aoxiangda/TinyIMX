# ACK 线协议批量扩展的安全基础（PERF-218）

2026-10-21 更新至 R238。状态：隔离 SDK 批量 API、Main 可选路径已实现；SDK 与 Main 两层各 18 项真实 gRPC 检查通过；Main 四组真实 Broker、128 条消息验收通过。后续独立同镜像 Proxy 又完成正常/坏回执两组四轮、共256条，实际确认后端ACK32次单条→3次批量，同时确认坏回执失败范围1→16条，最终均恢复。默认仍 OFF，原运行程序、SDK及Proxy配置未替换；没有新的完整负载 CPU、P99 或容量成绩。

## 已核对的实际源码差异

TinyIMX 的旧路径（当前默认）使用 `AwaitBoundedAckBatch`，逐条调用官方 SDK `asyncAck`。这是有界并行 N 次调用，不是一个带 N 个 entry 的请求。R224 已在独立开关后接入真正的多 entry 请求。

固定 C++ SDK `da8271b572b41b627bcbbdfb5b3310c0c5e4736b` 中，`SimpleConsumerImpl::wrapAckRequest` 每次加入一个 entry；`ClientManagerImpl::ack` 的回调只有一个 `std::error_code`。其 `MULTIPLE_RESULTS` 分支按聚合成功处理，但回调不携带 response entries，顶层无法再解析。这构成新增批量请求时的障碍。固定代理的单 entry 正常请求不会产生混合状态；本次没有据此声称现有单 entry 业务已经丢消息。

固定服务端 `98799686a99c85537cf0a13648b0b521bd16a6b1` 的 `AckMessageActivity` 有两条实现：配置开启时调用 broker 批量接口，否则在代理内逐 entry 发起处理；两者都收集逐条 `AckMessageResultEntry`。不同状态集合返回 `MULTIPLE_RESULTS`。服务端批量实现还可能通过客户端回执缓存取到续期后的回执，所以响应 receipt 文本可以与请求不同。

服务端源码保存在 `PERF167-mq-batch-primary-r1/`，API 协议固定为 `bc8e1845a8b0a1b5ec828690324401a323244354`。源码存在不等于当前代理配置或实际运行类字节已完整核验；后续接入必须核对实际镜像/配置并实跑，不能仅按默认值归因。

## 本轮已实现并执行

`patches/rocketmq-cpp-5.1.1/BatchAckResult.h`：

- 每个协议请求 1–256 项；限制 topic/group、消息 ID 和 receipt 长度，拒绝空值、NUL、重复 ID 和未支持的 lite-topic 路由。
- 以唯一 message ID 映射回原输入位置，允许响应乱序与服务端合法续期回执；不按数组下标或回执原文本对应结果。
- OK/MULTIPLE_RESULTS 必须有完整逐条状态；缺项、重复、外来 ID、缺失状态及聚合矛盾均不返回成功。
- 整批权限、认证及其他 RPC 级错误覆盖全部输入，不能被一个矛盾的成功 entry 推翻。
- 超出计数上限的无效协议对象不会按攻击者提供的 N 分配错误数组。对有界正常请求保持一项输入一项结果。

`tests/eventing/rocketmq_batch_ack_result_test.cpp`：34 项命名检查；另做 500 轮 2–256 项、随机响应顺序、真实 protobuf 序列化/反序列化及不修改输入对象的检查。Release 和 ASan/UBSan 各 1,534 项检查通过。自身 header/test 被 sanitizer 编译，原静态 protobuf 依赖未插桩，不能声称整个 SDK 的 sanitizer 覆盖。

R218 当时的证据为 `PERF218-batch-ack-result-qualification-r1/`；两版 SDK、静态协议库、2,292 个容器、保护 SQL 和 binlog 均未改变。该次仅为协议解析器验收。后续接入、实际提交和限定 Broker 验收见下文，仍无完整性能收益证明。

## 扩展的实现约束及实际落实

1. 新增 SDK 的非虚同步 `SimpleConsumer::ackBatch`，由内部异步分组执行并等待所有终态。公开类型使用原 Message 共享指针和逐项 error_code，不改现有对象布局或 ClientManager 虚表。
2. 内部复用 `manager()->getRpcClient(target, true)`、现有签名元数据、客户端身份及 TLS/channel；不能复用会丢失逐项结果的旧 `ClientManager::ack` 聚合回调。实际新增非虚 `RpcClientImpl::asyncAckBatch`，共享原 manager callback 队列，使用 shared-owned context；不能调用会 `delete this` 的旧 `InvocationContext::onCompletion()`。
3. 按 target endpoint/topic 分组，每个协议请求内 ID 唯一。若上层出现同 ID 不同回执，拆到不同请求，保持原输入位置，不能合并成一个结果或静默丢项。空/无效 handle 保持该位置失败，其他合法项仍可处理。
4. 所有分组共享一次网络截止时间；请求与 Message handle 保持到对应 callback 完成；回调去重、所有分组收齐才返回，Stop 仍在返回后外部顺序调用。异常、超时、权限失败和协议结构错误都不能转成成功，不能使用脱离生命周期的后台线程。原回调队列的排队可能延长 API 返回时间，未承诺独立于队列的严格端到端时限；不支持并发 Stop 与 ACK。
5. 提供窄的版本化 C 导出 getter 返回同步能力表，校验 magic、版本、结构尺寸、GNU libstdc++ C++11 ABI 和上限。Main 通过可选能力查询使用它，默认 OFF 保持旧路径及原 SDK 兼容；开启但缺能力时显式失败，不能静默回退后宣称减少 RPC。
6. 在新隔离 SDK 目录按固定 R191 编译命令重编 SimpleConsumer、SimpleConsumerImpl、RpcClientImpl 三个对象，校验其余原对象/静态依赖再链接。原 exports.map 的 `*rocketmq*` 已包含 getter，无需修改；新增 codec 用 hidden visibility，未泄露私有 protobuf 符号。原 SDK、R191 候选和 138 份缓存不变。新库 OFF/ON 使用同一个二进制，另做原库兼容检查。

真实验证必须包括：实际 gRPC 服务端收到的请求数/entry 数；乱序与混合结果；缺项、重复/外来 ID、权限拒绝、超时、共享截止时间、异常与停止；真实 Broker 正常 ACK、漏确认重投和无重复业务生效。注意代理批量分支会先解码全部 receipt，格式损坏可能导致整批失败，测试不能预设坏格式一定能得到逐条部分成功；确定的部分成功语义可由受控真实 gRPC 对端验证。

接入和限定验证完成后，仍要按原 Windows 发压拓扑做完整 42,000 条、后台闭环和反向控制，测 CPU/P99 决定是否保留。先完成冻结 R175/R212 限页单项实验，再独立验证线协议 ACK 批量，固定日志延迟开关，避免多项变化合并计算收益。无外键、CID/MID、持久化边界、权限和业务协议保持原要求。

## R221—R229 的具体结果

| 问题 ID | 已执行动作和结果 | 证据 | 比较及下一项 |
|---|---|---|---|
| PERF-221 / 222 | 新增非虚批量 API、版本化能力表、端点/topic/唯一 ID 分组、逐条结果映射与原回调队列；三个新对象隔离链接，旧公共符号未移除 | `PERF222-sdk-wire-ack-build-r3/result.json` | 原库不变；初版薄 public header 缺 Configuration.h 的失败保留，复制原完整 public headers 后通过。最终库 SHA 为 `6f9b0a661651bb7120209c2842005b50c8daa24cd2dca3cae6450b61208a90f4` |
| PERF-223 | SDK 实际 gRPC 18 用例、服务端收到 18 请求/385 entries；乱序、合法续期、部分失败、缺/重/外来 ID、矛盾聚合、认证/权限错误、双端点及超时通过 | `PERF223-sdk-wire-ack-functional-r1/result.json`、`server/server-requests.jsonl` | 两个超时分组共享 4 秒网络预算，API 约 4 秒返回；身份元数据仅检查存在，未把此项描述成独立的密码学验证 |
| PERF-224 | Main 实际接入；只新编 consumer 对象，producer 对象逐字节保留；独立重链接 Message/relay/projector 三程序；9 角色各用原/新 SDK，共 18 加载检查通过；能力契约 32 项 × 4 配置通过 | `PERF224-main-wire-ack-build-r1/result.json` | 其余原 Main archives 及原九程序保留；不是九程序全部重编。明确开启但无能力/ABI 不兼容时在创建 SDK client 前失败；模糊失败不回退重发 ACK |
| PERF-225 | 同一候选 SDK、日志优化固定 OFF；Main OFF/ON/ON/OFF，四个新主题共 128 条真实 Broker 消息通过；每组刻意漏一条，均真实重投，成功条目超过 10 秒不可见期后无重投；停止后失败、缺能力和非法开关拒绝通过 | `PERF225-main-wire-ack-real-mq-r1/result.json`、`points.json` | 两组 ON 均 32 个有效条目、3 次 SDK 批量提交。OFF 的新 API 指标为 0，仅表示未调用新增 API，绝不是没有发旧 ACK；真实 Broker 用例的计数来自 SDK，不能当独立服务端抓包 |
| PERF-226 | 再次连续 10 秒只读内存复查，可用 5.337–5.367 GiB，未执行正式驱动 | `PERF226-host-recheck-r1/result.json` | 未达原 8 GiB 门槛；此前 R213 自动审批拒绝仍有效，未绕过。待实际达门槛才恢复正式控制 |
| PERF-227 / 229 | 从运行 Proxy 读取实际 jar/class，反汇编核对两分支；实际配置无覆盖，当前启动日志 `enableBatchAck=false`，没有改变配置 | `PERF227-installed-proxy-ack-audit-r1/result.json`、`PERF229-proxy-startup-ack-flag-r1/result.json` | 实际启动值已核对；按此字节码应走逐条 Broker 调用。没有单次方法 trace，不宣称 Broker 内部聚合已经发生；服务端开关只能作为后续独立变量 |
| PERF-228 | 同样 18 用例通过实际 Main adapter，服务端仍为 18 请求/385 entries；保留逐条部分失败、错误信息及停止后失败 | `PERF228-main-wire-ack-controlled-r1/result.json` | 补齐 Main 到 SDK 的异常传递验证；不把受控对端成功替代整个 IM 的顺序/权限/故障恢复 |

两个阶段的 ACK 必须区分：此改动是 MQ 消费者确认，不是改变客户端“消息持久化 ACK”的成功边界。没有去掉事务、Outbox 或可靠性步骤，也没有把前台确认提前到入队；其整机 CPU 和前台 P99 收益仍要由完整负载实测判断。

## 推广前剩余项

1. 保持默认 OFF；SDK 的 GNU libstdc++ C++11 ABI 与固定源码、依赖组合绑定，不宣称通用第三方 ABI 扩展。
2. 先完成已冻结限页实验，再冻结此项精确 Main + SDK 组合，以同一候选 OFF/ON/ON/OFF 比较。日志延迟、未读快照 RPC、Proxy 服务端 batch 开关分别固定，不合并归因。
3. 完整核验 42,000 条消息、所有本地索引、实际 MQ 全量事件、Redis 投影、Retry/DLQ 与权限/顺序，之后比较 Windows 发送计划起算的 ACK/投递、VM 忙核和组件资源。
4. 如果完整负载未形成有效批量或 P99/资源退化，保留负结果并维持 OFF；不能将本次刻意形成 16 项批次的合并比例套到正式流量。持续容量、极限 QPS、长时间稳定性和 WuKongIM 最终追平仍未完成。

## 已封存的可复现实验组合

R231 已将本轮全部源代码、失败和成功结果存入 E，353 个运行文件（168,671,023 字节）实际独立恢复并逐一校验；Linux symlink 另存明确映射，不冒称 Windows 已创建同类链接。保护检查确认原 2,292 容器、20 个健康运行服务、原/新程序、原 SDK/lazy SDK 及 138 缓存均保留，原业务库、九个指定测试库和 binlog 不变。

R232 在 E 盘从已验证的 R185/R231 恢复内容组装完整九角色包：六个原字节程序、三个本轮重新链接程序，以及固定新 SDK、原隔离依赖标记、公开 headers、LICENSE/NOTICE。43 个普通文件再次从压缩包实际恢复并逐项核对，两个 Linux 库链接映射一并校验。该包使用原角色镜像和 SDK 库挂载约定，尚未部署；它不覆盖先前冻结 R175/R212 的独立限页实验。

- 证据清单：`<LOCAL_EVIDENCE_ROOT>/wire-ack-r231/manifest.json`，SHA-256 `7f51cac087d72577d4db309ea6909320f3c825d52dfbc3d1367531f8df7d4da7`。
- 九角色待测包：`<LOCAL_EVIDENCE_ROOT>/wire-ack-runtime-r232/nine-role-wire-ack-runtime.tar.gz`，SHA-256 `1ff9d5ddb518a2a7674adb1bf9a3153eb1c0f304b38f6edf25d8341b55dc652e`。
- 精确程序/SDK 组合：`wire-ack-runtime-r232/package/runtime-manifest.json`，SHA-256 `6c0079907180eb40878afe1b6032a3c2c1c0a7fb898068186a8f5310031878de`。

本轮没有删除任何 guest 程序、缓存、容器、数据库或快照；原正式发压门槛不变，资源资格不足时不启动正式轮。

R234—238继续闭环了“只合并到Proxy、内部是否仍N次”的具体问题：同镜像临时Proxy在相同客户端/SDK下切换服务端开关，实际TCP重组的Remoting请求与group核对后，OFF两组各32次单条，ON两组各3次批量。正常128条均恢复；另128条坏回执对照确认整批失败扩大、全量恢复，但未证明应用投影只生效一次。原配置保持false。字节分析还拆清了4次系统心跳发送，并保留命名宽度差异的分析限制。详见 [后端实际分支、请求与工程代价](PROXY_ACK_BRANCH_20261021_R235.md)。这仍不能替代正式整机性能对照。

R243继续用实际原生store/relay、RPC和Redis projector验证中间坏回执后重投及持久已读推进，128条消息最终一致。R247为正式projector增加停止后静止SDK统计（Stop之前读取），process_global_new_api_only，避免将缺能力/OFF的0误判为总RPC为0；R248原SDK/新SDK OFF/ON及18真实gRPC用例通过，实际18请求385条目逐项匹配SDK。原冻结包与默认值保留，仍无正式CPU/P99收益。详见 [恢复、统计与失败记录](NATIVE_ACK_RECOVERY_STATS_20261021_R243.md)。

R255/256实际镜像复验纠正了容器加载证据范围：镜像LD_LIBRARY_PATH优先选择/opt/tinyimx/lib旧SDK，R247的6项MQ loader日志（包含old/wire标签）均解析到该旧库。此前18项检查仅证明依赖可加载，不证明新SDK选中。显式优先路径、三个MQ角色的只读挂载与实际选中SHA已在9项镜像对照通过；原/新库6项正向控制符合。宿主侧显式候选路径的功能实验仍有效。独立SDK正式控制器还需接入绑定并核对运行映射，未启动完整性能轮。详见 [实际镜像路径缺陷及修正](SDK_IMAGE_BINDING_20261021_R255.md)。
