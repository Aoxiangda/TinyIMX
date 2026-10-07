# TinyIMX 正常服务器与 Windows 多客户端启动

2026-10-07，Qt6 0.3 功能完善版。后端压测保持暂停。本指南对应 `qt6-ui-features-20261007-attempt9` 的正常业务客户端。

## 一键启动

Ubuntu VM 保持开机，当前地址 `192.168.220.128`。

1. Windows 工作区双击 `Start-Server.cmd`，看到 `READY`、`running: 20`、`expected: 20`。
2. 双击 `Start-AI.cmd`，确认 Ollama `http://127.0.0.1:11434` 和已安装模型 `qwen3:0.6b`。
3. 双击 `Start-3-Clients.cmd`，打开三个独立窗口。单窗口使用 `Start-Client.cmd`。
4. 预填的三个用户名分别输入演示密码 `123456` 并登录。

| 账号 | 用户 ID | 演示密码 |
|---|---:|---|
| desktop_alice_20261007 | 750001 | 123456 |
| desktop_bob_20261007 | 750002 | 123456 |
| desktop_carol_20261007 | 750003 | 123456 |

以上是任务创建的演示账号，已有双向好友关系。没有更改或重置旧账号密码。验收另用 `features_*_20261007` 三个专用账号（750004–750006），其测试消息和群不混入以上正常演示用户。

也可直接运行 `evidence/qt6-ui-features-20261007-attempt9/build/tinyimx_desktop.exe`。Qt DLL、`platforms`、`qml` 文件需一起保留。当前默认为真实服务器，`--demo` 才进入设计预览。旧版本和旧窗口不会自动替换；自行关闭旧窗口后再启动新程序。

PowerShell：

```powershell
.\tools\Start-TinyIMX-Server.ps1
.\tools\Start-TinyIMX-AI.ps1
.\tools\Start-TinyIMX-Desktop.ps1 -Count 3
```

只检查服务：

```powershell
.\tools\Start-TinyIMX-Server.ps1 -StatusOnly
Test-NetConnection 192.168.220.128 -Port 9000
Invoke-RestMethod http://192.168.220.128:18082/health
```

Ubuntu：

```bash
python3 /home/jackson7/projects/TinyIMX_publish/clients/qt6/server/start_existing_server.py --start
```

## 操作顺序

1. 私聊：Alice 选 Bob 并发送，Bob 回复。Bob 退出登录后 Alice 再发；Bob 登录后检查离线补投和历史。服务器“已保存”、投递 ACK、已读表示不同阶段。
2. 好友：联系人页输入数值用户 ID 申请，对方刷新或等待 15 秒，在申请列表接受/拒绝；删除好友由服务器校验。
3. 群聊：Alice 在群组页创建群，邀请 Bob/Carol；选择群进入聊天。开放群可按群 ID 加入。群主/管理员可管理成员，服务器核验权限。
4. 群管理：角色、禁言/解除、资料更新、群主转让、退出、解散均走真实 RPC。群主不能直接退出，先转让或解散。资料更新使用已读取版本，冲突时刷新再提交。
5. 文件：先选目标会话，再点击附件/选择文件。256 KiB 分块上传，终验后出现分享卡片。接收者点击下载；任务页可暂停、继续、重试、取消和打开所在目录。下载的 UUID 文件名不会覆盖已有文件。
6. AI：AI 页点击“应用并检测”，输入问题并发送，可停止。勾选会话上下文后才附加当前已加载最近 20 条消息；不会自动操作业务数据。AI 失败可继续聊天。

## 地址、保存位置与恢复

| 内容 | 位置 |
|---|---|
| TIMX 聊天连接 | `192.168.220.128:9000` |
| 文件/群历史入口 | `http://192.168.220.128:18082/desktop`，健康检查 `/health` |
| Windows 本地模型 | `http://127.0.0.1:11434`，`qwen3:0.6b` |
| 正常下载 | Windows 下载目录的 `TinyIMX/UUID-文件名` |
| 任务记录 | Qt AppLocalDataLocation 下 `transfers/SHA256(入口与账号)/tasks.json` |
| 原始验收/源码快照 | Windows `evidence/qt6-*` |
| 服务启动和部署审计 | Ubuntu 项目 `.local/codex/` |

掉线后重新登录。未确认消息沿用原 CID 重试，防止再次创建消息；群历史通过认证入口恢复并检查当前成员和原投递快照。传输恢复时重新检查上传源的整文件哈希、服务器缺块进度或下载偏移，损坏下载在整文件校验失败后可以创建新任务重试，原失败文件保留。

同机同入口同用户名的新版窗口使用 QSharedMemory 原子占用防止误多开。跨主机/跨网关的新登录会在旧连接下一次心跳确认 epoch 不匹配后通知退出；客户端正常心跳为 5 秒，这不是瞬时互踢保证。Redis 故障不会被当成确认替换。

## 启动保护与资源

服务器助手按已验收清单核对 20 个常驻容器的 ID、镜像 ID、挂载来源和读写模式。运行中的容器保持运行；停止的原容器按依赖顺序启动并等待健康检查。一次性 rocketmq-init 成功退出不需重新运行。身份变化/容器缺失时拒绝重建并保留审计。

助手不执行 Compose 重建、拉取镜像、删除卷或清空数据库。VM 重启后若 Docker 没启动，在 Ubuntu 终端使用 `sudo systemctl start docker` 再执行助手。VM IP 变化须重新核对绑定地址、SSH 和客户端地址，文件入口当前明确绑定 VM 的私网 IP。不要用盲目重建部署处理连接错误。

AI 助手只检查现有服务，端口空闲才以隐藏窗口启动已安装 Ollama；并行生成和驻留模型数各 1，仅设置子进程环境。不重下载模型、不结束其他应用、不改全局变量。小模型用于真实功能链路，答案质量未做独立评估。

## 已知范围

64 MiB 单文件、100 本地传输任务、新群 100 成员；任务和授权附件为当前实现。传输任务异常日志被保留且不覆盖；取消期间如服务掉线显示未确认，重新登录后重试完成服务器取消。文件授权有效期 7 天，文件服务登录会话 8 小时；过期后重新登录/由拥有者重新分享。

群持久化已读回执、在线状态订阅、全量好友/会话分页、持久化草稿、TLS 客户端和跨设备同步仍待完善。当前使用私网明文入口，不能直接作为公网生产部署。完整问题分析见 [功能完善记录](FEATURE_COMPLETION_20261007.md)，公开验收见 `clients/qt6/validation/FEATURES_20261007.json`。本轮没有重新运行容量压测。
