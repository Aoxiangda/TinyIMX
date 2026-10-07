# TinyIMX 正常服务器与 Windows 多客户端启动

2026-10-07。后端压测保持暂停。本轮提供正常业务客户端，不启动容量 worker。

## 现在直接使用

Ubuntu VM 保持开机，当前地址 `192.168.220.128`。实际服务器已经运行，Windows 客户端连接 `192.168.220.128:9000`。

在 Windows 工作区双击 `Start-Server.cmd` 检查或启动已经审计的原有部署，再双击 `Start-3-Clients.cmd` 打开三个独立窗口。三个窗口会预填不同用户名；分别输入演示密码 `123456`，点击登录。

| 账号 | 用户 ID | 密码 | 窗口 |
|---|---:|---|---|
| desktop_alice_20261007 | 750001 | 123456 | Alice |
| desktop_bob_20261007 | 750002 | 123456 | Bob |
| desktop_carol_20261007 | 750003 | 123456 | Carol |

以上为本轮新建的演示账号。没有读取、更改或重置旧账号密码。三组好友关系通过真实 TIMX 好友申请、接受建立，登录后可在会话或联系人页直接选择对方。

客户端完整目录为工作区 `evidence/qt6-ui-live-20261007-attempt9/build`，程序是 `tinyimx_desktop.exe`。DLL、`platforms`、`qml` 等文件需要与 exe 一起保留。旧 `qt6-ui-20261007-attempt3` 程序是 UI 原型，不用于真实通信。双击新 exe 可以再次打开窗口；手动填写不同用户名。默认连接真实服务器，`--demo` 明确切换为设计预览。

PowerShell 在工作区执行：

```powershell
.\tools\Start-TinyIMX-Server.ps1
.\tools\Start-TinyIMX-Desktop.ps1 -Count 3
```

只检查服务、不启动已停止容器：

```powershell
.\tools\Start-TinyIMX-Server.ps1 -StatusOnly
Test-NetConnection 192.168.220.128 -Port 9000
```

在 Ubuntu 终端也可以执行：

```bash
python3 /home/jackson7/projects/TinyIMX_publish/clients/qt6/server/start_existing_server.py --start
```

## 操作验证

1. Alice 窗口选择 Bob，发送一段中文或多行文本；Bob 窗口选择 Alice 查看并回复。
2. Carol 与两人分别互发，验证多个账号和多个进程同时通信。
3. 在 Bob 设置页退出登录，Alice 发给 Bob；Bob 重新登录后查看离线消息和服务器历史。
4. “已保存”来自服务器持久化确认，接收 ACK 不等于已读。已读状态可以通过重新选择会话读取服务器历史更新。
5. 添加其他联系人时，填写数值用户 ID；接收者在联系人页点击刷新并接受申请。好友申请列表后台每 15 秒刷新，也可手动刷新。

## 正常服务器启动保护

启动助手只检查 19 个常驻容器，逐个核对已记录的容器 ID、镜像 ID、挂载来源和读写模式。运行中的容器不会重启；停止的原容器按依赖顺序启动，等待健康检查。RocketMQ 初始化容器为一次性任务，正常退出，不会被启动助手反复启动。

助手不会执行 `docker compose up`、拉取镜像、构建服务、重建容器、修改配置、删除卷或清空数据库。缺少原容器、镜像或挂载身份变化时会拒绝操作并要求重新审计。每次启动审计保存在 Ubuntu 项目 `.local/codex/desktop-server-start-*`。

如果 Ubuntu 重启后 Docker 尚未启动，可在 Ubuntu 终端使用 `sudo systemctl start docker`，然后运行助手。VM IP 变化时需要相应更新客户端地址和 SSH 连接配置；当前启动脚本固定使用本轮核实的地址。不要通过盲目运行 Compose 重建已有部署来处理连接问题。

## 已接入范围和边界

真实 Qt 客户端已接入账号认证、本人身份、好友与申请、私聊发送/入库确认、接收与尝试 ACK、历史分页、未读/已读、离线补投、定时心跳。网络使用 Qt 异步 socket，TIMX 分包/粘包由独立 codec 处理。发送超时保留原 CID，手动重试不创建新的客户端消息身份；历史与实时投递按 MID 合并。

此版群组、文件、AI 的客户端操作还未接入，真实模式显示接入说明；设计预览仍保留原有页面。没有将演示结果当作这些功能的服务器结果。联系人在线状态尚未订阅，显示“在线状态未订阅”；请求/好友的大规模完整分页、持久化本地草稿、跨设备同步、TLS 客户端配置仍待完善。当前客户端选择的是本机 VM 的 9000 明文入口，9443 TLS 入口未接入此版客户端。

本机同一服务器、同一用户名只能在一个新版客户端登录或连接中，使用 Qt 原生共享内存对象的原子创建占用检查（1 字节，退出释放），在认证请求发送前拒绝重复登录。不同账号可正常多开。此保护作用于本机新版客户端，不能代表服务器已实现跨主机/跨网关的单会话互踢。本轮真实双网关联调发现同账号跨网关旧连接未立即退出，已保留失败证据；后端修复不属于此次暂停压测后的客户端交付，仍列为未解决问题。

## 问题复盘

| 问题 | 证据/原因 | 解决或当前状态 |
|---|---|---|
| 旧 exe 不会真实聊天 | DemoStore 仅有本地合成数据，没有 TCP 认证与消息通路 | 新增 LiveStore 和 TIMX codec，默认真实模式 |
| Qt 登录立刻失败 | native E2E 显示 `The proxy type is invalid for this operation`；同入口单次原始 TCP 登录成功 | 仅此 socket 设置 NoProxy；不改主机全局代理 |
| 消息、历史竞态可能重复显示 | 发送 CID 行与提前到达的历史 MID 行可同时存在 | 入库确认以 MID 合并，重试沿用 CID；重复投递仍确认各自 seq |
| 客户端关闭出现堆损坏 | Windows 事件 1000、异常 0xc0000374；socket 成员析构发出 disconnected 时状态成员已释放 | 显式析构先停 timer/断开回调/abort socket，再析构成员；验收包含退出码 |
| 部署审计数量最初不符 | Compose 项目还包含已经成功退出的 rocketmq-init | 精确核对 19 常驻角色，保留失败预检；没有写旧用户数据 |
| 同账号跨网关旧连接未立即下线 | 真实 E2E 的 same_account_replaces_old_session 失败 | 本机客户端重复登录预先拦截；服务端跨网关问题仍未修复 |
| Windows 命名通道不具备所需排他性 | QLocalServer 允许多个同名监听，重复登录测试失败 | 改为 QSharedMemory 原子创建，退出/认证失败时仅释放本进程拥有的对象 |
| 加入本机账号占用后首次编译失败 | QT_NO_CAST_FROM_ASCII 禁止 QChar(char) | 改为显式 UTF-16 字符，保留 attempt7 失败日志 |

所有构建目录、源快照、原始失败日志、功能测试记录和截图保留在 Windows `evidence/qt6-*`。源代码和最终公开验收凭据提交至 Ubuntu 原 Git 分支，使用明确文件清单，保持之前的三处未提交修改及 `.env.example` 不变。测试期间只新增三个演示账号和属于这些账号的正常好友/消息记录；不自动清理历史测试数据。

最终验收事实以 `clients/qt6/validation/LIVE_CLIENT_20261007.json` 为准，容量和性能预期没有因功能测试而被宣称达标。
