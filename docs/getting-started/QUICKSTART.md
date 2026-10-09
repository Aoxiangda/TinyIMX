# TinyIMX 公开源码构建与联调

本指南面向第一次克隆仓库的开发者，覆盖界面预览、Linux 后端、Windows 客户端、演示账号、文件入口和本地 AI。默认使用自己的独立开发部署，不复用原作者的数据库、容器身份或私有配置。

## 1 获取完整项目

```bash
git clone https://github.com/Aoxiangda/TinyIMX.git
cd TinyIMX
```

`main` 包含完整源码。源码公开不等于已附带可执行安装包；后端二进制、Qt DLL、模型和数据库由使用者构建 / 配置。

## 2 先预览客户端

1. 安装 Qt 6.8+，选择支持 Qt Quick、QuickControls2、Svg、Network 的桌面 Kit。已验证环境为 Qt 6.10.2 MinGW 64-bit。
2. Qt Creator 打开 `clients/qt6/CMakeLists.txt`，选择 Release 并构建。
3. 在运行参数中填写 `--demo`，即可查看独立样例界面，不需要服务器或模型。
4. 正常模式去掉 `--demo`，使用自己的后端地址和已创建账号。

客户端使用 C++17，后端使用 C++20，两个 CMake 工程独立构建。

## 3 Linux 后端依赖与基础构建

以下以 Ubuntu 和 Bash 为例。需要支持 C++20 的编译器、CMake 3.24+、Python 3、Docker Engine / Compose v2，以及足够的源码编译空间。完整 vcpkg 与 RocketMQ SDK 构建较重，建议从低并行度开始。

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config git curl \
  zip unzip tar autoconf automake libtool bison flex libssl-dev zlib1g-dev \
  python3 openssl netcat-openbsd
```

Docker 按自己的发行版安装；当前用户必须能够执行 `docker info` 和 `docker compose version`。不要为了跳过权限问题把 Docker socket 改成所有用户可写。

### vcpkg

在新克隆项目中引导独立工具链；已有工具链可直接设置 `VCPKG_ROOT`，无需再次克隆：

```bash
mkdir -p toolchains
git clone https://github.com/microsoft/vcpkg.git toolchains/vcpkg-tinyimx
export VCPKG_ROOT="$PWD/toolchains/vcpkg-tinyimx"
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
```

`vcpkg.json` 保存依赖与 baseline，`CMakePresets.json` 从 `VCPKG_ROOT` 读取 toolchain。依赖包括 gRPC / Protobuf、OpenSSL、hiredis、libmysql、ZooKeeper 与 OpenTelemetry。

```bash
cmake --preset linux-release
cmake --build --preset build-release --parallel 2
```

按需要运行已构建测试：

```bash
ctest --test-dir build/linux-release --output-on-failure
```

部分模块是外部服务集成测试，未启动对应数据库 / Redis / RPC 服务时可能失败；请查看该模块测试或验收脚本的前置条件。基础构建默认不代表 RocketMQ 可靠投递环境已准备完成。

## 4 首次独立部署

以下步骤面向新的独立部署。原作者环境的 `start_existing_server.py` 只恢复原容器，不能用于创建你的新环境。

### 环境与状态目录

复制 `deploy/production/.env.example` 到同目录 `.env`，替换全部 `CHANGE_ME`。已有 `.env` 时先检查内容，不覆盖自己的配置。

```bash
if [ ! -e deploy/production/.env ]; then
  cp deploy/production/.env.example deploy/production/.env
  chmod 600 deploy/production/.env
else
  printf '%s\n' 'Existing .env retained; inspect and edit it before deployment.'
fi
```

编辑以下字段，使用自己生成的强凭据：

- `TINYIMX_MYSQL_PASSWORD`、`TINYIMX_MYSQL_ROOT_PASSWORD`
- `TINYIMX_REDIS_PASSWORD`、`TINYIMX_MCP_TOKEN`
- 新环境可使用默认 `COMPOSE_PROJECT_NAME=tinyimx-m21`；如与其他部署共存，需核对项目名、端口及历史验收脚本中的固定容器名后再调整。
- 本地开发设置 `TINYIMX_DEPLOYMENT_ENV=dev`。

基础 SQL 当前使用 `tinyimx` 数据库名，首次部署保持 `TINYIMX_MYSQL_DATABASE=tinyimx`。不要只改环境变量就假设所有 SQL 的 `USE` 语句也已改变。

```bash
export TINYIMX_M21_STATE_DIR="$HOME/.local/share/tinyimx/public-demo"
export TINYIMX_RUNTIME_UID="$(id -u)"
export TINYIMX_RUNTIME_GID="$(id -g)"
export TINYIMX_BUILD_JOBS=1
mkdir -p "$TINYIMX_M21_STATE_DIR"
chmod 700 "$TINYIMX_M21_STATE_DIR"
```

这些是当前 shell 的变量，后续部署命令在同一 shell 执行；新开终端时重新设置。状态目录不存入 Git。

### 构建与启动

```bash
bash scripts/run_m21_production_contract_gate.sh
bash scripts/m21_build_runtime_image.sh
bash scripts/run_m21_production_runtime_gate.sh
bash scripts/m21_production_status.sh
```

运行镜像构建会引导隔离的 RocketMQ C++ SDK、启用对应 CMake 选项，并收集动态库。SDK 版本和 gRPC 依赖族独立，避免混入 TinyIMX 自身较新的 RPC 工具链。

部署会创建业务配置、开发自签名 TLS 材料、数据库 / 队列卷，并启动服务。开发证书用于本地验收；当前 Qt 客户端使用可信私网 TCP 入口。

`m21_prepare_workspace.sh` 现在只审计磁盘与缓存占用，不删除构建树或编辑器缓存。部署预检要求至少 8 GiB 可用磁盘；完整首次编译与镜像下载建议预留更多空间。

### 基础端口

| 端口 | 用途 | 默认范围 |
| --- | --- | --- |
| 9000 | TIMX 明文 TCP | 服务器监听；用于可信私网客户端 |
| 9443 | NGINX TLS TCP | 服务端 TLS 入口，Qt 尚未自动使用 |
| 18081 | NGINX `/healthz` | 服务器回环 |
| 18080 | MCP `/health`、`/mcp` | 服务器回环 |
| 19090 | Prometheus | 服务器回环 |
| 13133 | Collector 健康检查 | 服务器回环 |
| 18082 | 文件 / 群历史入口 | 可选 overlay，默认服务器回环 |

## 5 启用桌面文件与群历史入口

运行镜像包含 `desktop_file_server`。它与聊天 Gateway 独立，调用 User / File / Message / Group 服务。桌面客户端从聊天服务器主机派生固定的 `18082` 端口，因此当前跨机器联调保持该端口不变。

```bash
mkdir -p "$TINYIMX_M21_STATE_DIR/desktop-file"
chmod 700 "$TINYIMX_M21_STATE_DIR/desktop-file"
```

在 `.env` 中设置 `TINYIMX_DESKTOP_BIND_ADDRESS`：同机客户端保持 `127.0.0.1`；Windows 连接 Ubuntu VM 时填写 VM 的可信私网 IP，例如 `192.168.1.10`。不必绑定所有接口。

```bash
docker compose --env-file deploy/production/.env \
  -f deploy/production/docker-compose.yml \
  -f deploy/production/docker-compose.desktop.yml \
  up -d desktop-file
```

第一次启动会在私有状态目录生成 32 字节、0600 权限的签名密钥。保留它用于验证已有分享凭证；不要把运行中密钥提交到 Git。

```bash
# 按实际绑定地址替换。
curl --fail http://192.168.1.10:18082/health
```

## 6 创建自己的演示账号

客户端没有自助注册入口。公开源码不附带原作者的业务数据库，历史交接文档中的账号也不会自动存在于你的新库。

确认 MySQL 容器已健康，先只读预检。默认 Compose 项目对应 `tinyimx-m21-mysql-1`；若改了项目名，传入自己的容器名。

```bash
python3 clients/qt6/server/create_demo_accounts.py \
  --mysql-container tinyimx-m21-mysql-1 --prefix demo_
```

出现 `PREFLIGHT_PASS` 后，明确执行插入并交互输入三账号的密码：

```bash
python3 clients/qt6/server/create_demo_accounts.py \
  --mysql-container tinyimx-m21-mysql-1 --prefix demo_ --apply
```

助手创建 `demo_alice` / `demo_bob` / `demo_carol`，每个账号使用独立随机 salt。它不修改已有用户、不重置密码、不插入好友关系。用户名冲突时拒绝操作，可换一个 `--prefix`。审核与生成的 SQL 位于仓库 `.local/demo-accounts/`，不保存明文密码。

输出包含数据库分配的用户 ID。登录使用用户名；好友申请使用用户 ID，通过客户端完成申请 / 受理流程。

## 7 Windows 客户端构建与分发

Qt Creator 是推荐入口。也可在已初始化 Qt / 编译器环境的终端中执行：

```powershell
cmake -S clients/qt6 -B build/qt6 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt6 --parallel 2
ctest --test-dir build/qt6 --output-on-failure
```

若 `find_package(Qt6)` 失败，选择正确 Qt Kit，或用 `-DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/mingw_64"` 指定自己的 Qt 路径；不能混用 MinGW 和 MSVC 编译器 / 库。

```powershell
# UI 预览。
.\build\qt6\tinyimx_desktop.exe --demo

# 真实联调，分别开启三个不同账号窗口。
.\build\qt6\tinyimx_desktop.exe --endpoint "192.168.1.10:9000" --username "demo_alice"
.\build\qt6\tinyimx_desktop.exe --endpoint "192.168.1.10:9000" --username "demo_bob"
.\build\qt6\tinyimx_desktop.exe --endpoint "192.168.1.10:9000" --username "demo_carol"
```

分发前运行 Qt 部署工具：

```powershell
windeployqt --release --qmldir clients/qt6/qml build/qt6/tinyimx_desktop.exe
```

复制整个部署目录，包含 Qt DLL、编译器运行库、`platforms` 与 QML 插件，不能只拷贝 EXE。[Qt 官方部署说明](https://doc.qt.io/qt-6/windows-deployment.html)解释了依赖收集和 `--qmldir`。

## 8 本地 AI

安装 Ollama 后，按机器资源选择模型。已有功能验证使用 `qwen3:0.6b`；它用于轻量功能联调，答案质量和模型吞吐另行评估。

```bash
ollama pull qwen3:0.6b
ollama list
```

若 Ollama 尚未运行，执行 `ollama serve`；已有服务时直接使用。客户端 AI 页填入 `http://127.0.0.1:11434` 和模型名，点击检测后发送问题。`127.0.0.1` 指运行客户端的 Windows 主机，不是 VM。

桌面 AI 与后端 Agent 分别配置。后端 Agent 使用 `.env` 中 `TINYIMX_AI_ENDPOINT` / `TINYIMX_AI_MODEL`，必须在容器网络里实际可达；不要把桌面回环地址照搬到容器。MCP 静态服务令牌不下发到桌面客户端。

参考：[Ollama 快速开始](https://docs.ollama.com/quickstart)、[vcpkg 官方入门](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-bash)。

## 9 业务联调清单

1. Alice 与 Bob 申请 / 受理好友，确认双向私聊权限。
2. 发送多行和 Unicode，分别观察持久化、投递与已读状态。
3. Bob 下线后继续发送，重新登录检查离线补投和历史。
4. 创建群、邀请 Carol，检查角色、禁言、转让、退出与历史权限。
5. 发送一个测试文件，暂停 / 续传 / 下载，检查整文件校验。
6. 切换账号确认任务隔离；新窗口登录同账号检查窗口锁。
7. 启用 Ollama，按需勾选会话上下文，测试流式响应和取消。

这些是正常功能联调，不替代万人容量、故障注入或长期稳态测试。

## 10 常见问题

| 现象 | 检查顺序 |
| --- | --- |
| `main` 部署被旧分支名拦住 | 更新到当前 main；生产入口已改为核对源码工作区，而非固定分支名 |
| Qt 找不到 DLL / 平台插件 | 运行 `windeployqt`，完整复制部署目录，核对编译器 Kit |
| 登录失败 | 服务器地址、9000 可达性、账号是否由自己创建、用户名与密码 |
| 聊天可用，文件 / 群历史失败 | 是否启用 desktop overlay；18082 绑定地址与防火墙；四个下游 RPC 服务健康 |
| 账号助手提示用户名已存在 | 使用新前缀；不要重置已有账号 |
| AI 连接失败 | 客户端所在机器的 Ollama、模型列表、配置的地址与端口 |
| 数据库缺表 | 首次卷初始化是否成功；已有库应审阅迁移，不能靠删卷重新初始化 |
| 性能不同于 README | 对齐提交、开关、连接数、请求率、时间窗、硬件与压测机资源 |

## 11 数据保存

MySQL、Redis、RocketMQ 等持久化数据使用 Compose 卷；服务配置、TLS、文件和签名状态位于 `TINYIMX_M21_STATE_DIR`。Windows 下载位于系统下载目录的 `TinyIMX/`，传输任务位于 Qt 应用本地数据目录。

Git 和源码快照不能替代业务数据备份。停止服务保留卷，不使用 `down -v` 或自动删除现有数据库 / 文件来排错。
