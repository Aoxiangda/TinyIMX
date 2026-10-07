# TinyIMX Desktop · Qt6 0.3

C++ / Qt Quick 原生 Windows 客户端，默认连接 Ubuntu VM 的真实 TinyIMX 后端。本轮接入群组、群消息、文件传输与本机 Ollama，保留已有认证、好友与私聊功能。`--demo` 和 `--capture` 使用隔离的设计样例；功能验收使用 `LiveStore`、真实 RPC、数据库、文件字节及模型生成。

## 当前工作区直接启动

1. 开启 Ubuntu VM，双击工作区根目录 `Start-Server.cmd`；它核对并启动已验收的 20 个现有容器。
2. 双击 `Start-AI.cmd` 检查本机 Ollama。已有服务继续使用；端口空闲时才启动已安装的 `ollama serve`。不会再次下载模型。
3. 双击 `Start-Client.cmd` 打开一个窗口，或 `Start-3-Clients.cmd` 打开三个独立账号窗口。
4. 演示用户名为 `desktop_alice_20261007`、`desktop_bob_20261007`、`desktop_carol_20261007`，演示密码都是 `123456`。已有好友关系，可直接互相发消息。

当前通过验收的完整程序目录为 `evidence/qt6-ui-features-20261007-attempt9/build`，程序名 `tinyimx_desktop.exe`。复制程序时保留整个目录中的 DLL、`platforms` 和 `qml`。旧程序保持在各自证据目录中；已经打开的旧窗口需要自行关闭并重新打开新版。

连接地址：聊天 `192.168.220.128:9000`；文件与群历史 `http://192.168.220.128:18082`；默认 AI `http://127.0.0.1:11434` / `qwen3:0.6b`。文件服务地址由聊天地址的主机名和固定端口 18082 派生。

## 已实现功能

| 页面 | 真实功能 |
|---|---|
| 消息 | 私聊与群聊、服务器入库确认、实时投递 ACK、MID/CID 去重、原 CID 失败重试、私聊历史与未读/已读、离线消息；群历史当前成员及原投递快照校验 |
| 联系人 | 好友列表、申请发送与接受/拒绝、删除好友、数值 ID 添加、打开私聊；15 秒刷新申请列表 |
| 群组 | 创建、开放加入、邀请、移出、角色变更、禁言/解除、版本化资料修改、群主转让、退出、解散；群组与成员自动分页 |
| 文件 | 原生文件选择器、256 KiB 上传块、64 KiB 下载范围、逐块和整文件 SHA-256、暂停/续传、失败重试、服务器确认取消、账号任务持久化、私聊/群聊授权附件卡片 |
| AI | 真实 Ollama 流式生成、模型列表检测、停止生成、错误提示；可选附加当前已加载会话最近 20 条消息；生成期间聊天保持可用 |

从消息页选择私聊或群聊后上传，服务器完成文件终验才发送附件卡片。接收者点击卡片下载，整文件 SHA-256 通过后才标记完成。文件页保留失败任务和未完成文件用于复盘；下载默认保存到 Windows“下载/TinyIMX”。

文件操作的身份由独立 HTTP 入口在登录后派生，禁止客户端指定任意 actor；分享能力绑定收件用户或群组。没有把旧 MCP 的静态服务令牌写入客户端。当前 AI 为直接模型问答与只读会话片段，不提供自动修改好友、群组和文件的工具调用。

## 构建与复现

使用本机已有 **Qt 6.10.2 MinGW 64-bit**，未安装新工具链。Qt Creator 打开本目录 `CMakeLists.txt` 并选择该 Kit。Windows 工作区根目录执行：

```powershell
.\tools\Build-TinyIMX-Desktop.ps1 -EvidenceName qt6-ui-my-build -Capture
.\tools\Start-TinyIMX-Desktop.ps1 -EvidenceName qt6-ui-my-build -Count 3 -Editor
```

构建助手为每次构建保留新的源码快照、SHA-256、日志、状态/协议自检与原生窗口截图。只修改当前进程 PATH 并恢复，不改系统环境。编译为 Release，构建并行度 2。

功能自检需要属于测试的三账号 JSON（endpoint、password、accounts），证据目录必须不存在。不要对真实用户执行测试中的群管理和文件取消操作。

```powershell
.\evidence\qt6-ui-features-20261007-attempt9\build\tinyimx_desktop.exe --features-test <owned-profile.json> --evidence <new-evidence-directory>
.\evidence\qt6-ui-features-20261007-attempt9\build\tinyimx_desktop.exe --live-test <owned-profile.json> --evidence <another-new-directory>
```

## 验收与边界

本轮事实、问题复盘和已知限制见 [功能完善记录](../../docs/client/FEATURE_COMPLETION_20261007.md)，正常使用见 [启动与操作指南](../../docs/client/NORMAL_SERVER_WINDOWS_CLIENT_20261007.md)，公开检查结果见 [FEATURES_20261007.json](validation/FEATURES_20261007.json)。完整原始记录保留在 Windows `evidence/qt6-*` 和 Ubuntu 项目 `.local/codex/`；包含临时授权的原始事件及私有配置不进入 Git。

当前单文件上限 64 MiB，每账号本地最多 100 个传输任务；新建群默认成员上限 100。群已读/未读仅为当前客户端状态，尚无持久化群已读回执。联系人在线订阅、大规模好友/会话完整分页、持久化草稿、跨设备任务同步和客户端 TLS 仍待实现。默认入口用于可信本机/VM 私网；上线公网需要完成 TLS、授权会话容量和入口限流的专门评估。

本轮是功能联调。压测按用户要求暂停，检查通过不代表 10k–50k 用户性能验收。
