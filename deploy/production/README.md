# TinyIMX Compose 部署

NGINX → 双 Gateway → ZooKeeper 发现的 User / Social / Message / Group / File 服务。消息与 Outbox 以 MySQL 持久化，Outbox Relay → RocketMQ → Unread Projector → Redis 构成异步事件链；OpenTelemetry Collector 与 Prometheus 提供观测。

## 新部署

完整依赖、环境、账号和客户端步骤见 [公开快速开始](../../docs/getting-started/QUICKSTART.md)。先从 `.env.example` 创建自己的 `.env`，替换全部占位凭据，并设置独立 `TINYIMX_M21_STATE_DIR`。

```bash
bash scripts/run_m21_production_contract_gate.sh
bash scripts/m21_build_runtime_image.sh
bash scripts/run_m21_production_runtime_gate.sh
bash scripts/m21_production_status.sh
```

命令从仓库根目录执行。构建脚本按源码工作区校验，支持 main；完整运行包包含 `desktop_file_server`。预检和运行脚本不会为你创建业务账号，使用快速开始中的新账号助手。

## 桌面文件入口

`docker-compose.desktop.yml` 是可选 overlay，用于授权文件和群历史 HTTP 入口。创建自己的状态子目录并设置接口地址后启用：

```bash
mkdir -p "$TINYIMX_M21_STATE_DIR/desktop-file"
chmod 700 "$TINYIMX_M21_STATE_DIR/desktop-file"
docker compose --env-file deploy/production/.env \
  -f deploy/production/docker-compose.yml \
  -f deploy/production/docker-compose.desktop.yml up -d desktop-file
```

默认绑定 127.0.0.1:18082；跨机器客户端应配置 `TINYIMX_DESKTOP_BIND_ADDRESS` 为服务器可信私网 IP。当前 Qt 由聊天主机派生固定 18082 端口。

## 状态与凭据

- `TINYIMX_M21_STATE_DIR/config`：私有业务配置。
- `TINYIMX_M21_STATE_DIR/tls`：TLS 材料。
- `TINYIMX_M21_STATE_DIR/file-data`：服务端文件字节。
- `TINYIMX_M21_STATE_DIR/desktop-file`：授权签名状态。
- Compose 卷：MySQL、Redis、RocketMQ、ZooKeeper、Prometheus 持久化数据。

凭据、运行状态、数据库与密钥不进入 Git。基础 SQL 当前使用 tinyimx 数据库名。空白 MySQL 卷按挂载的 schema 与文件迁移初始化；已有库需要审阅迁移，不能通过删卷修复。

## TLS 与 AI

开发自签名证书只服务本地验收。当前 Qt TCP / HTTP 客户端用于可信私网，不因 NGINX 提供 TLS 就自动具备 TLS。

桌面 AI 直接连接客户端机器的 Ollama；可选后端 Agent 通过 `scripts/m21_ai_query.sh` 请求驱动执行，独立配置容器可达的模型端点和模型名。后端 MCP 提供身份约束的只读工具。

## 停止与恢复

`scripts/m21_production_down.sh` 保留数据卷；不要使用 down -v 排错。原作者的 `clients/qt6/server/start_existing_server.py` 核对历史容器 ID / 镜像 / 挂载，仅适用于原环境恢复，不是新用户首次部署工具。

`m21_prepare_workspace.sh` 仅输出磁盘与缓存占用，保留构建目录和编辑器缓存。本次 main 发布未在空白机器执行完整冷部署；组件构建与运行输出应逐项检查。
