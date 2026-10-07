# TinyIMX：C++ 即时通信后端与 Qt6 客户端

当前已接入原生Windows客户端的私聊、群聊、好友申请、持久化历史、文件分块/校验/续传，以及本机Ollama流式问答。Qt6 0.3正常交互通过；10k–50k全部功能容量仍须分别验收，压测目前暂停。

## 先找到项目、账号与数据

- [项目总手册：账号、数据、功能、启动、组件、记录与边界](docs/client/PROJECT_HANDOVER_20261007.txt)
- [正常服务器与多客户端启动](docs/client/NORMAL_SERVER_WINDOWS_CLIENT_20261007.md)
- [Qt工程与客户端功能/构建](clients/qt6/README.md)
- [功能问题复盘](docs/client/FEATURE_COMPLETION_20261007.md)
- [容量事实与验收边界](docs/performance/CAPACITY_OPTIMIZATION_AND_ACCEPTANCE_20261007.md)
- [问题总台账](docs/performance/ISSUES.md)

正式Git项目位于Ubuntu `/home/jackson7/projects/TinyIMX_publish`；Windows工作区 `source` 是源码镜像。工作区根目录入口为 `Start-Server.cmd` / `Start-AI.cmd` / `Start-Client.cmd` / `Start-3-Clients.cmd`。

已验证Windows客户端为工作区 `evidence/qt6-ui-features-20261007-attempt9/build/tinyimx_desktop.exe`。DLL、platforms、qml需跟随整个目录保留。演示用户名 `desktop_alice_20261007`、`desktop_bob_20261007`、`desktop_carol_20261007`，ID750001/750002/750003，演示密码均 `123456`；登录用用户名，添加好友用ID。`features_*`是三个独立验收账号，未改动真实旧账号密码。

## 实际数据与服务

| 数据 | 准确位置 |
|---|---|
| MySQL业务库 | 容器 `tinyimx-m21-mysql-1` / 库 `tinyimx` / 卷 `tinyimx-m21_mysql-data` |
| MySQL物理目录 | `/var/lib/docker/volumes/tinyimx-m21_mysql-data/_data` |
| 服务器文件/分块 | `/home/jackson7/.local/share/tinyimx/m21/file-data` |
| 私有运行配置 | `/home/jackson7/.local/share/tinyimx/m21/config` |
| 文件分享签名状态 | `/home/jackson7/.local/share/tinyimx/desktop-file-20261007` |
| Windows传输任务 | `%LOCALAPPDATA%/TinyIMX/TinyIMX Desktop/transfers` |
| 正常接收文件 | Windows下载目录的TinyIMX；当前本机为 `D:/Downloads/TinyIMX` |
| 原始证据/前像 | Windows工作区 `evidence`；Ubuntu项目 `.local/codex` |

后端20个正常容器包括两个Gateway、五个业务服务、独立文件/群历史入口、Outbox/未读投影、MySQL/Redis/ZooKeeper/RocketMQ/Nginx及观测组件。聊天入口 `192.168.220.128:9000`，文件/群历史 `http://192.168.220.128:18082`；Windows Ollama是独立进程，默认 `127.0.0.1:11434` / `qwen3:0.6b`。启动助手恢复本机已验收容器，不能代表空白机器的一键新部署。

## 真实功能与边界

私聊、好友申请接受/拒绝、群管理/历史、授权文件与续传、真实AI流式问答已接入。删除好友、注册/改密/资料编辑、持久化群已读、全量好友/会话分页、在线订阅、持久化草稿/AI历史、Qt TLS和完整MCP Agent尚未交付。设计样例 `--demo` 与真实业务数据独立，完整入口/限制见总手册。

## 验证、性能与版本

- [公开功能检查](clients/qt6/validation/FEATURES_20261007.json)：正常交互通过，非容量验收。
- [每轮源码/结果](clients/qt6/validation/ITERATIONS_20261007.json)：成功与失败保留。
- [本次交接核验](clients/qt6/validation/HANDOVER_20261007.json)：数据目录/账号/声明核对。

历史万人指定合同通过；两万人P99与大群尾延迟未通过，五万全部功能未验收。不同提交或合同不能拼成同一成绩。Git仅保存代码/说明/去敏结果；源码快照、旧容器和Git bundle不代替MySQL与附件备份，完整业务数据一致性备份尚未确认。
