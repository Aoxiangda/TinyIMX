<p align="center"><img src="docs/assets/tinyimx-banner.svg" alt="TinyIMX 高并发即时通信与智能协作系统" width="100%"></p>

<h1 align="center">TinyIMX</h1>
<p align="center"><strong>高并发即时通信与智能协作系统</strong></p>
<p align="center">Linux C++ 后端 · Windows Qt6 原生客户端 · 可靠消息 · 授权文件共享 · 本地 AI</p>

<p align="center">
  <img src="https://img.shields.io/badge/Backend-C%2B%2B20-00599C?style=flat-square&amp;logo=cplusplus&amp;logoColor=white" alt="C++20 后端">
  <img src="https://img.shields.io/badge/Desktop-Qt6%20%2F%20QML-41CD52?style=flat-square&amp;logo=qt&amp;logoColor=white" alt="Qt6 / QML">
  <img src="https://img.shields.io/badge/Network-epoll%20%2F%20Reactor-4968D8?style=flat-square" alt="epoll / Reactor">
  <img src="https://img.shields.io/badge/Deployment-Docker%20Compose-2496ED?style=flat-square&amp;logo=docker&amp;logoColor=white" alt="Docker Compose">
  <img src="https://img.shields.io/badge/AI-Ollama%20%2B%20MCP-323C4E?style=flat-square" alt="Ollama 和 MCP">
</p>

<p align="center">
  <a href="#产品体验">产品体验</a> · <a href="#功能全景">功能全景</a> · <a href="#系统架构">系统架构</a> ·
  <a href="#快速开始">快速开始</a> · <a href="#性能与验证">性能与验证</a> · <a href="#文档导航">文档导航</a>
</p>

---

## 项目介绍

**TinyIMX 把实时聊天、群协作、授权文件与本地 AI 放进同一个原生桌面工作流。** 用户可以添加好友、进行私聊和群聊、恢复离线消息、查阅历史、共享文件，再按需使用 AI 助手理解当前会话。

后端以 C++20 实现网络连接、业务调度与可靠消息链路：epoll / Reactor 管理 TCP 长连接，双 Gateway 连接五个 gRPC 业务域；MySQL 维护业务事实，可选原生私聊配置提供同步原子提交，RocketMQ / Redis 连接事件与投影。Qt 6 / QML 提供原生交互，OpenTelemetry 与 Prometheus 支持观测。

**仓库默认分支提供完整源码、协议、数据库结构、部署脚本、Qt 6 工程、测试和性能复盘。** 克隆默认分支即可查看完整实现；本地业务数据库、私有配置、模型权重与编译产物由使用者自行创建。

<!-- verified-performance:start -->
| 50,000 | 1,250 条/s | 100% | 0 |
| :---: | :---: | :---: | :---: |
| 真实认证在线连接 | 已验证业务消息速率 | 375,000 条完整成功 | 收尾待发布与待确认 |

> 8 vCPU / 16 GiB Ubuntu，128 B 私聊，持续 300 秒；原生同步 WAL、真实 MQ 投影与接收确认。另完成 1,000 条/s 工作点。两档 ACK 平均为 9.258 / 13.290 ms，P99 为 48.573 / 60.553 ms。完整条件与正文吞吐见[50K 实测报告](docs/performance/FINAL_CAPACITY_20261027.md)。
<!-- verified-performance:end -->

[**查看产品介绍**](docs/PRODUCT_OVERVIEW.md) · [**浏览产品展示页**](docs/product/index.html) · [**阅读完整迭代复盘**](docs/performance/ITERATION_HISTORY_20261004_20261027.md)

## 产品体验

### 消息与协作

原生三栏工作区整合会话、私聊 / 群消息、历史与附件卡片。消息区分服务器持久化、接收确认和已读状态，支持多行文字与 Unicode。

![Qt6 真实服务联调：群消息、会话列表与授权附件卡片](clients/qt6/preview-features/messages.png)

<details>
<summary><strong>展开查看群管理、文件任务与 AI 助手</strong></summary>

### 群管理

建群、加入、邀请、成员角色、禁言、群主转让、退出和解散，通过服务端权限校验落到真实业务。

![Qt6 群协作页面](clients/qt6/preview-features/groups.png)

### 文件任务

独立通道支持分块、暂停 / 续传、取消 / 重试与完整性校验，完成上传后再向会话发送授权附件卡片。

![Qt6 文件传输任务页面](clients/qt6/preview-features/files.png)

### 本地 AI

Ollama 流式问答支持模型检测、停止生成，以及由用户选择是否附加当前已加载会话片段。

![Qt6 Ollama 真实模型问答页面](clients/qt6/preview-features/ai.png)

</details>

以上为已保存的真实服务功能联调截图，使用专用演示 / 验收账号；[设计预览](clients/qt6/preview)使用独立样例数据。截图中的群名、消息和文件为验收样例。

## 功能全景

| 模块 | 用户可以做什么 | 实现要点 |
| --- | --- | --- |
| 账号与会话 | 密码登录、心跳保活、重新登录、识别其他位置登录 | 加盐 PBKDF2-HMAC-SHA256；Redis 在线路由与 SessionEpoch；同机同账号窗口锁 |
| 好友关系 | 按用户 ID 申请、接受 / 拒绝、查看列表并发起私聊 | 私聊鉴权；好友受理事务；双向关系一致性 |
| 私聊消息 | 实时收发、多行 / Unicode、失败重试、离线补投 | TIMX 帧协议；持久化 ACK；CID 幂等与 MID 去重；接收端确认 |
| 会话与历史 | 会话列表、未读数、历史分页、私聊已读 / 未读 | 权威历史数据；Redis 未读投影；有界批量读取 |
| 群协作 | 建群、开放加入、邀请 / 移出、角色、禁言 / 解除、资料修改、转让 / 退群 / 解散 | 群组与成员分页；版本冲突检查；当前成员与原投递快照约束历史访问 |
| 文件共享 | 私聊 / 群聊附件、分块上传、断点续传、取消 / 重试、下载 | 256 KiB 上传块；分块 / 整文件 SHA-256；HMAC 授权；终验后分享 |
| 原生桌面 | 多账号、多窗口、会话切换、文件任务与消息交互 | Qt Quick / QML；异步 TCP / HTTP；按账号隔离传输任务；QSharedMemory |
| AI 助手 | 流式问答、模型检测、生成取消、可选会话上下文 | 当前已加载最近 20 条消息按需附加；模型失败不阻断聊天 |
| MCP 扩展 | 受控读取资料、好友、会话 / 历史、群组 / 成员、文件元数据 | 8 个只读工具；服务端派生身份；gRPC 权限校验与期限传递 |
| 运维与观测 | 健康检查、指标、链路分析、源码 / 结果复盘 | Compose；OpenTelemetry Collector；Prometheus；版本化验收记录 |

### 一条完整的使用路径

1. Alice 与 Bob 登录不同客户端，通过好友申请建立关系。
2. Alice 发送私聊；服务端先持久化，再异步投递，Bob 接收后确认。
3. Bob 离线期间，Alice 继续发送；Bob 重新登录后恢复待确认消息与历史。
4. Alice 建群并邀请 Bob / Carol，使用群角色与禁言规则协作。
5. 上传文件，完成 SHA-256 终验后分享；接收者通过授权附件卡片下载。
6. 用户按需启用本地 AI，选择是否附加当前会话片段进行问答。

## 系统架构

```mermaid
flowchart TB
  Desktop[Windows Qt6 / QML]
  Nginx[NGINX TCP / TLS 入口]
  GA[Gateway A · Reactor]
  GB[Gateway B · Reactor]
  subgraph Domain[五个 gRPC 业务服务]
    User[User · 认证与资料]
    Social[Social · 好友与权限]
    Message[Message · 私聊与群消息]
    Group[Group · 群与成员]
    File[File · 文件元数据与分块]
  end
  MySQL[(MySQL · 业务事实 / SQL Outbox)]
  Redis[(Redis · 在线路由与未读投影)]
  ZK[ZooKeeper · 服务发现]
  Relay[Outbox Relay]
  MQ[RocketMQ]
  Projector[Unread Projector]
  Ingress[独立文件 / 群历史 HTTP 入口]
  MCP[MCPServer · 8 个只读工具]
  Agent[可选后端 AI Agent]
  Ollama[本地 Ollama]
  OTel[OpenTelemetry Collector]
  Prom[Prometheus]
  Desktop -->|TIMX / TCP| Nginx
  Nginx --> GA & GB
  GA & GB -->|gRPC| Domain
  Native[(可选原生私聊存储 · 同步 WAL)]
  Domain --> MySQL
  Message --> Native
  Native --> Relay
  GA & GB --> Redis
  Domain -. 注册与发现 .-> ZK
  MySQL --> Relay --> MQ --> Projector --> Redis
  Desktop -->|分块文件 / 群历史| Ingress
  Ingress --> User & File & Message & Group
  Desktop -->|流式问答| Ollama
  Agent --> MCP --> Domain
  Agent -->|模型推理| Ollama
  Domain -. 指标与追踪 .-> OTel --> Prom
```

桌面 AI 当前直接调用 Ollama；后端 Agent / MCP 是独立扩展链路。文件字节不进入聊天 Gateway 的消息执行队列，独立入口负责派生登录身份并调用对应服务。

### 核心技术栈

| 层次 | 技术与职责 |
| --- | --- |
| 后端构建 | **C++20**、Linux、CMake、vcpkg；以根目录构建配置为准 |
| 网络与并发 | epoll、EventLoop、Reactor、TCP、TIMX、有界执行器、同键 FIFO |
| 服务协作 | gRPC / Protobuf、ZooKeeper 动态发现、双 Gateway 路由 |
| 数据与事件 | MySQL、可选原生私聊存储、Redis、持久事件 / Outbox、RocketMQ |
| 桌面客户端 | **C++17 / Qt6 / QML**；Qt 6.8+，已有验证环境为 Qt 6.10.2 MinGW 64-bit |
| 文件与身份 | SHA-256、HMAC、PBKDF2-HMAC-SHA256、服务端身份派生 |
| AI 扩展 | Ollama、流式响应、MCP、OpenAI-compatible 后端 Provider |
| 部署与观测 | Docker Compose、NGINX、OpenTelemetry、Prometheus |

## 快速开始

```bash
git clone https://github.com/Aoxiangda/TinyIMX.git
cd TinyIMX
```

| 目标 | 操作入口 |
| --- | --- |
| 先看客户端界面 | Qt Creator 打开 `clients/qt6/CMakeLists.txt`，构建后以 `--demo` 启动 |
| 构建 Linux 后端 | 配置 vcpkg，使用 `cmake --preset linux-release` |
| 联调真实多客户端 | 部署后端、启用桌面文件入口、创建自有演示账号，再连接服务器 |
| 查看性能与工程细节 | 阅读性能证据、模块源码与复盘文档 |

完整步骤见 **[公开源码构建与联调指南](docs/getting-started/QUICKSTART.md)**，包含依赖、首次部署、账号创建、Qt 打包、模型配置与排错。

### Linux 后端

准备支持 C++20 的工具链、CMake 3.24+ 与 vcpkg，将 `VCPKG_ROOT` 指向已引导的 vcpkg 目录：

```bash
cmake --preset linux-release
cmake --build --preset build-release --parallel 2
ctest --test-dir build/linux-release --output-on-failure
```

这是基础构建；带 RocketMQ 的完整运行镜像由 `scripts/m21_build_runtime_image.sh` 构建，SDK 和共享库闭包单独处理。部分集成测试需要外部服务，按模块准备环境后执行。

### Windows Qt6

推荐使用 Qt Creator：打开 `clients/qt6/CMakeLists.txt`，选择含 Quick、QuickControls2、Svg、Network 的 Qt6 Kit，构建 Release。

```powershell
# 在客户端构建输出目录中执行。
.\tinyimx_desktop.exe --demo

# 改为自己的 Linux 服务器私网地址和新建账号。
.\tinyimx_desktop.exe --endpoint "192.168.1.10:9000" --username "demo_alice"
```

不同账号可各开一个窗口；同一入口、同一用户名限制重复窗口。分发时使用 `windeployqt` 收集 DLL、QML 和平台插件，保留完整部署目录。

### 部署与账号

从 [`.env.example`](deploy/production/.env.example) 创建 `.env`，设置自己的数据库、Redis 与 MCP 凭据；运行状态使用独立目录，凭据不进入 Git。

```bash
# 在新的独立部署中执行，先完成 QUICKSTART 的依赖与环境配置。
bash scripts/run_m21_production_contract_gate.sh
bash scripts/m21_build_runtime_image.sh
bash scripts/run_m21_production_runtime_gate.sh
```

文件 / 群历史入口通过 [桌面 Compose overlay](deploy/production/docker-compose.desktop.yml)启用，默认绑定 `127.0.0.1:18082`，跨机器连接时显式配置可信私网接口。

客户端不提供自助注册。[演示账号助手](clients/qt6/server/create_demo_accounts.py)支持先只读预检，再显式 `--apply` 并交互输入密码创建三个新账号；不修改已有账号、不重置密码、不直接插入好友关系。

## 可靠性与权限设计

### 从保存到已读

```mermaid
sequenceDiagram
  participant A as 发送客户端
  participant G as Gateway
  participant M as MessageService
  participant DB as 选定消息存储
  participant B as 接收客户端
  A->>G: Send(CID, recipient, content)
  G->>M: 鉴权后的消息请求
  M->>DB: 原子提交消息和待发布事件
  DB-->>M: 同步提交完成
  M-->>G: MID / 持久化结果
  G-->>A: 持久化 ACK
  Note over G,DB: 消息及 Outbox 支撑异步投递与恢复
  G-->>B: 实时投递或登录后补投
  B->>G: 接收 ACK(MID)
  G->>M: 更新接收状态
  B->>G: 私聊已读确认
  G->>M: 推进已读状态
```

- **确认边界：** 服务器入库 ACK、接收端 ACK、已读是不同阶段。
- **重试身份：** 发送方与 CID 唯一约束避免重复建消息，客户端按 MID 去重。
- **原子性与事件：** SQL 路径使用消息 / Outbox 同事务提交；可选原生路径同步提交消息、幂等、索引与待发布事件。RocketMQ 驱动后台处理，Redis 承担可重建投影。
- **异常恢复：** 未确认消息可补投，ACK 丢失允许重试，重复投递按幂等规则处理。

### 会话、群与文件

- **会话治理：** 心跳、Redis 在线路由与本地 SessionEpoch 配合，确认登录替换后精确失效旧连接；Redis 故障不直接视为登录替换。
- **并发边界：** 有界执行器限制积压，同键 FIFO 约束本执行器内同键任务顺序，支持过载拒绝、取消与优雅停机。
- **群历史 ACL：** 校验当前成员身份，并结合发送者 / 原投递快照约束可读记录，后入群不自动获得全部旧消息。
- **文件授权：** 分块 / 整文件双层校验；HMAC 凭证绑定接收者或群组，下载时再次核验身份 / 成员关系。
- **客户端恢复：** 上传 ID 迟到时补偿取消；损坏下载可创建新任务重下；传输日志按账号和入口隔离。

## 性能与验证

### 从真实业务测量容量

**8 vCPU / 16 GiB Ubuntu 虚拟机运行完整服务，Windows 原生负载端独立发压。** 采用 50,000 认证连接、128 B 私聊与 300 秒完整窗口；每条成功消息还核对正文、CID / MID、收件归属、接收确认、历史与未读索引、MQ 事件和首次投影。

<!-- capacity-summary:start -->
| 50,000 认证在线用户 · 128 B · 300 秒 | 1,000 条/s | 1,250 条/s |
| --- | ---: | ---: |
| 完整成功 | **300,000 / 300,000** | **375,000 / 375,000** |
| 含最后收尾的实测业务 QPS | **999.978** | **1,249.946** |
| 正文吞吐约值 | **128 KB/s · 1.024 Mbit/s** | **160 KB/s · 1.280 Mbit/s** |
| 持久化 ACK 平均 / P99 | **9.258 / 48.573 ms** | **13.290 / 60.553 ms** |
| 计划发送到投递 P99 | **49.596 ms** | **61.891 ms** |
| 整台虚拟机平均忙核 | **5.606 / 8 核** | **5.719 / 8 核** |
| 负 ACK / 超时 / 重复投递 | **0 / 0 / 0** | **0 / 0 / 0** |
| 收尾待发布 / 待确认 | **0 / 0** | **0 / 0** |
<!-- capacity-summary:end -->

业务 QPS 按完整消息计算，内部 ACK、心跳、RPC 与 MQ 操作不重复相加；正文吞吐不包含协议及中间件开销。两个 Gateway 均衡承载，本组每对私聊用户在同一 Gateway。上述为已完成的原生私聊配置工作点。默认 SQL 部署与该配置的职责、构建、迁移和开关分别说明，数据不跨配置套用。[最终容量与各负载条件](docs/performance/FINAL_CAPACITY_20261027.md)保留完整分母、每分钟延迟、资源和恢复结果。

### 优化有对照，结果有证据

- **减少会话列表往返：** 50 项读取复用一次健康连接和有界 Lua，网络命令 100 → 2；旧指定 10K 混合负载 ABBA 会话页 P99 保守下降 80.46%，精度、顺序和错误语义保持。
- **提交快照直接回复：** 复用原子提交产生的精确未读结果，减少 ACK 前重复读取；四轮 120 万条，控制 / 候选各 60 万条的 ACK P99 为 26.4602 / 23.0262 ms。
- **有界发布持续排空：** 通过发布进度、扫描范围和工作并行度治理后台成本，保留真实 MQ 与独立投影；最终工作点要求前台、后台和持久状态一起闭合。
- **工程验证：** 当前配置经过 207 项业务检查与 23 项 MCP 检查；另保存幂等、权限、截止时间、取消 / 停机及受控进程恢复实验，各自注明配置与覆盖范围。

[完整迭代复盘](docs/performance/ITERATION_HISTORY_20261004_20261027.md)连接问题、具体改动、实验比较与采用决策；[全量问题索引](docs/performance/iteration-records/INDEX.md)和[实验目录](docs/performance/experiment-records/INDEX.md)保留技术过程。

## AI 与 MCP 工具

**桌面侧：** Ollama 流式问答、模型检测、停止生成、可选当前会话上下文；只包含当前已加载的最近消息，勾选后才发送给配置的模型服务。

**后端侧：** MCPServer 暴露以下只读能力，供独立 Agent 调用。身份从服务端认证上下文派生，参数不能任意指定 actor。

| 工具 | 能力 |
| --- | --- |
| `tinyimx.user.get_self_profile` | 当前用户资料 |
| `tinyimx.social.list_friends` | 好友列表 |
| `tinyimx.message.list_conversations` | 会话列表 |
| `tinyimx.message.list_history` | 可访问的消息历史 |
| `tinyimx.group.get` | 群信息 |
| `tinyimx.group.list_my_groups` | 当前用户群组 |
| `tinyimx.group.list_members` | 授权群成员 |
| `tinyimx.file.get_metadata` | 授权文件元数据 |

桌面 AI 当前不自动执行这些 MCP 工具，也不会自动修改业务数据。后端 Agent 的模型端点与模型名需独立配置；推理性能按机器和模型分别评估。

## 项目结构

```text
TinyIMX/
├── common/                 # 配置、日志、网络、并发、数据库、缓存与观测
├── gateway/                # 连接、TIMX 路由、会话与业务接入
├── services/               # 五个业务服务、RPC 与 AI/MCP
├── proto/                  # Protobuf / gRPC 契约
├── clients/qt6/            # Qt6/QML 桌面、协议、自检与截图
├── deploy/production/      # Compose、模板、NGINX、观测与桌面 overlay
├── db/migrations/          # 数据库迁移
├── scripts/                # 构建、部署、校验、诊断与历史验收
├── tests/                  # 单元、契约与模块验证
├── benchmark/              # 负载与证据工具
├── docs/                   # 快速开始、功能、性能与复盘
├── examples/               # 服务可执行入口与示例
└── CMakeLists.txt          # Linux 后端构建入口
```

## 文档导航

| 想了解什么 | 文档 |
| --- | --- |
| 新机器获取、构建、部署与联调 | [公开源码快速开始](docs/getting-started/QUICKSTART.md) |
| Qt6 功能、构建和打包 | [客户端说明](clients/qt6/README.md) |
| 后端 Compose、端口与数据目录 | [部署说明](deploy/production/README.md) |
| 正常业务功能与已知问题 | [功能完善记录](docs/client/FEATURE_COMPLETION_20261007.md) |
| 客户端验证结果 | [功能验收](clients/qt6/validation/FEATURES_20261007.json)、[迭代记录](clients/qt6/validation/ITERATIONS_20261007.json) |
| 性能优化、合同与失败 | [容量状态](docs/performance/CAPACITY_OPTIMIZATION_AND_ACCEPTANCE_20261007.md)、[问题台账](docs/performance/ISSUES.md) |
| 全项目复盘 | [项目复盘](docs/retrospective/PROJECT_REVIEW_20261008.txt) |
| 原开发环境账号与数据 | [历史交接手册](docs/client/PROJECT_HANDOVER_20261007.txt)、[原环境启动](docs/client/NORMAL_SERVER_WINDOWS_CLIENT_20261007.md) |
| 本次 main 发布与入口修正 | [发布记录](docs/retrospective/PUBLICATION_20261009.md) |

历史文档中的 VM IP、本机路径、演示账号和容器身份属于原开发环境，新部署以快速开始中的自有配置为准。

## 当前范围与后续计划

- [x] 私聊 / 群聊、好友受理、离线与历史、私聊已读 / 未读。
- [x] 群成员与角色治理、群历史 ACL、授权文件及续传。
- [x] 多账号 Qt6 客户端、Ollama 流式问答、后端只读 MCP。
- [x] 双 Gateway、gRPC 微服务、Compose 与可观测性。
- [ ] 自助注册 / 改密 / 资料编辑、删除好友。
- [ ] 持久化群已读、全量联系人 / 会话分页、在线状态订阅。
- [ ] 持久化草稿 / AI 历史、跨设备任务同步。
- [ ] Qt 客户端 TLS 与公网入口加固。
- [ ] 20k-50k 全功能容量、热点群、故障恢复与长期稳态验收。

当前单文件上限 **64 MiB**、每账号本地最多 **100 个文件任务**、新群默认成员上限 **100**。当前客户端明文入口用于可信私网联调；NGINX TLS 入口不代表 Qt 客户端已经启用 TLS。

## 参与与交流

欢迎通过 [Issues](https://github.com/Aoxiangda/TinyIMX/issues)反馈问题，或通过 [Pull Requests](https://github.com/Aoxiangda/TinyIMX/pulls)提交改进。建议附带提交版本、复现步骤、部署方式、脱敏日志和指标对应负载。

源码与文档公开，方便学习、复现和评审；仓库目前未声明独立开源许可证，使用与再分发授权可通过仓库 Issues 联系作者。

<p align="center"><strong>TinyIMX · 让沟通，有条不紊。</strong></p>
