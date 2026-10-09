# TinyIMX Desktop Qt6

Windows 原生 Qt Quick / QML 客户端，支持好友、私聊 / 群聊、历史与私聊已读、授权文件传输和本地 Ollama 流式问答。`--demo` 使用隔离样例，正常模式使用 LiveStore 与真实服务器。

## 构建与启动

Qt 6.8+，包含 Quick、QuickControls2、Svg、Network；已验证 Qt 6.10.2 MinGW 64-bit。Qt Creator 打开本目录 `CMakeLists.txt`，选择 Release。

也可在配置好的 Qt / 编译器终端运行：

```powershell
cmake -S clients/qt6 -B build/qt6 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt6 --parallel 2
ctest --test-dir build/qt6 --output-on-failure
.\build\qt6\tinyimx_desktop.exe --demo
.\build\qt6\tinyimx_desktop.exe --endpoint "192.168.1.10:9000" --username "demo_alice"
```

多账号分别开启窗口，同机同入口同用户名通过 QSharedMemory 限制重复窗口。服务器和文件入口分别为自有主机的 9000 与 18082；历史文档中的固定 VM IP 不用于新的部署。

## 打包

```powershell
windeployqt --release --qmldir clients/qt6/qml build/qt6/tinyimx_desktop.exe
```

保留整个目录中的 DLL、`platforms` 和 QML 插件，不能仅复制 EXE。详细依赖、部署、账号与模型见 [公开快速开始](../../docs/getting-started/QUICKSTART.md)。

## 功能

| 页面 | 功能 |
| --- | --- |
| 消息 | 私聊 / 群聊、持久化确认、接收 ACK、CID / MID 去重、失败重试、离线、历史与私聊已读 / 未读 |
| 联系人 | 列表、ID 申请、接受 / 拒绝、发起私聊 |
| 群组 | 建群 / 开放加入、邀请 / 移出、角色、禁言 / 解除、版本化资料、转让 / 退出 / 解散、群与成员分页 |
| 文件 | 256 KiB 上传、64 KiB 下载范围、SHA-256、暂停 / 续传 / 取消 / 重试、账号任务日志、授权附件卡片 |
| AI | Ollama 流式响应、模型检测、取消、可选当前已加载最近 20 条消息 |

上传终验通过后才分享，下载整文件校验通过后才完成。文件入口服务端派生身份，不向客户端下发 MCP 静态令牌。桌面 AI 不自动执行后端 MCP 工具。

## 验证与范围

- [真实功能联调截图](preview-features)
- [隔离设计预览](preview)
- [公开功能检查](validation/FEATURES_20261007.json)
- [迭代记录](validation/ITERATIONS_20261007.json)
- [功能问题与边界](../../docs/client/FEATURE_COMPLETION_20261007.md)

单文件 64 MiB，每账号本地 100 个文件任务，新群默认 100 成员。群未读为当前客户端状态；自助注册 / 改密 / 资料编辑、删除好友、持久化群已读、全量联系人 / 会话分页、在线订阅、持久化草稿 / AI 历史、跨设备任务同步和 Qt TLS 尚待实现。

[原开发环境启动文档](../../docs/client/NORMAL_SERVER_WINDOWS_CLIENT_20261007.md)中的 Start-*.cmd、助手、证据路径与演示账号属于原作者工作区，不是本仓库首次克隆的运行入口。
