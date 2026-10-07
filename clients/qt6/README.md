# TinyIMX Desktop · Qt6 客户端

可运行的 C++ / Qt Quick 桌面 UI 设计原型。使用本机既有 Qt 6.10.2 MinGW 64 编译，未安装新工具链。

本版具有聊天、联系人/好友申请、群管理、文件传输任务、AI 助手、设置、登录预览，以及窄窗口布局。
所有数据均为内存中的本地样例。账号登录、TIMX 网络协议、真实文件流和 Ollama / Agent 服务尚未接入。
状态“已保存 · 演示”不会被标为真实服务器保存；文件进度和 AI 回复均明确标注为模拟。

## 在当前 Windows 工作区运行

从工作区根目录运行：

```powershell
./tools/Build-TinyIMX-Desktop.ps1 -EvidenceName qt6-ui-my-build -Capture
./tools/Start-TinyIMX-Desktop.ps1 -EvidenceName qt6-ui-my-build -Editor
```

每次构建使用新的证据目录，保留之前的构建、日志、状态测试和截图。
构建脚本仅修改当前进程 PATH，并在结束时恢复；不会改变系统环境或 Qt 安装。
`-Editor` 同时在本机 Qt Creator 中打开本工程的 `CMakeLists.txt`。

可直接打开对应 `evidence/qt6-ui-*/build/tinyimx_desktop.exe`，旁边已有 Qt 运行库。
Qt Creator 应选择 **Desktop Qt 6.10.2 MinGW 64-bit** Kit；独立工程不依赖 Linux 后端 CMake。

## 页面与交互

- 消息：会话搜索、私聊/群聊切换、示例消息和附件、输入草稿、Enter/Shift+Enter/Ctrl+Enter、失败重试、禁言反馈、会话资料。
- 联系人：本地联系人搜索与资料、好友申请接受/拒绝、添加申请表单、跳转已有会话。
- 群组：群列表、成员/角色、版本信息、创建表单、邀请/加入入口、禁言预览、转让/退出/解散确认。
- 文件：全部/进行中/完成筛选，进度、暂停/继续/重试、取消确认，保留任务记录。
- AI：预设提问、生成中、停止、示例答复、离线失败、输入保留，以及待接入的上下文与工具区。
- 设置与登录：发送快捷键、通知偏好、连接状态预览、地址输入、密码遮罩，正式登录按钮保持禁用。

点击窗口右上角“演示在线”可切换离线。离线发送后恢复在线，再点击该消息的“重试”，可检查不新增重复消息的交互。
主导航快捷键为 Ctrl+1 到 Ctrl+5。窄窗口自动隐藏右侧资料区。

详细的信息架构、协议映射、异常流程、性能与安全要求见 [客户端设计规范](../../docs/client/QT6_CLIENT_DESIGN_20261007.md)。

## 代码结构

`qml/` 负责界面与交互；`src/DemoStore.*` 使用 `QAbstractListModel` 提供内存状态与合成数据；`assets/icons/` 是本任务绘制的 SVG 图标。
`src/main.cpp` 启动客户端，并提供状态契约自检和当前应用窗口截图入口。

此设计原型不表示后端 10k–50k 全功能性能验收完成。后端压测已按用户要求暂停。
