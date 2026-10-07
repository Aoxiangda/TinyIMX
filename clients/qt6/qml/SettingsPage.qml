import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    required property var shell
    ColumnLayout { anchors.fill: parent; anchors.margins: 32; spacing: 24
        ColumnLayout { spacing: 8; Text { text: "设置"; color: "#29364e"; font.pixelSize: 25; font.weight: Font.DemiBold } Text { text: "让客户端更贴合你的工作方式"; color: "#909cae"; font.pixelSize: 12 } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 14; color: "white"; border.color: "#e5e9f1"
                ColumnLayout { anchors.fill: parent; anchors.margins: 28; spacing: 25
                    RowLayout { Avatar { width: 62; height: 62; radius: 19; label: "翔"; tint: "#384b69" } ColumnLayout { spacing: 7; Layout.leftMargin: 15; Text { text: "敖翔"; color: "#53617a"; font.pixelSize: 21; font.weight: Font.DemiBold } Text { text: "个人资料 · 设计预览"; color: "#a3adbd"; font.pixelSize: 11 } } Item { Layout.fillWidth: true } UiButton { text: "登录页面"; onClicked: shell.showLogin() } }
                    Rectangle { height: 1; Layout.fillWidth: true; color: "#edf0f5" }
                    Text { text: "消息与输入"; color: "#68768f"; font.pixelSize: 14; font.weight: Font.DemiBold }
                    RowLayout { Layout.fillWidth: true; ColumnLayout { spacing: 6; Text { text: "Enter 发送消息"; color: "#7b889e"; font.pixelSize: 12 } Text { text: "关闭后使用 Ctrl + Enter；Shift + Enter 始终换行"; color: "#aab3c2"; font.pixelSize: 10 } } Item { Layout.fillWidth: true } Switch { checked: shell.enterToSend; onClicked: shell.enterToSend=checked } }
                    RowLayout { Layout.fillWidth: true; ColumnLayout { spacing: 6; Text { text: "通知偏好（本地预览）"; color: "#7b889e"; font.pixelSize: 12 } Text { text: "仅预览开关；系统通知接入时还需操作系统授权"; color: "#aab3c2"; font.pixelSize: 10 } } Item { Layout.fillWidth: true } Switch { checked: shell.notifyEnabled; onClicked: shell.notifyEnabled=checked } }
                    Rectangle { height: 1; Layout.fillWidth: true; color: "#edf0f5" }
                    Text { text: "连接与数据"; color: "#68768f"; font.pixelSize: 14; font.weight: Font.DemiBold }
                    RowLayout { Layout.fillWidth: true; Text { text: "界面连接状态"; color: "#7b889e"; font.pixelSize: 12 } Item { Layout.fillWidth: true } Switch { checked: demo.online; onClicked: demo.online=checked } }
                    Text { text: "切换状态可体验发送失败、手动重试、文件暂停和 AI 连接错误。"; color: "#aab3c2"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout { Layout.fillWidth: true; Text { text: "网关地址（待接入）"; color: "#7b889e"; font.pixelSize: 12 } Item { Layout.fillWidth: true } TextField { text: "192.168.220.128:9000"; implicitWidth: 240; font.pixelSize: 12; selectByMouse: true } }
                    RowLayout { Layout.fillWidth: true; Text { text: "界面主题"; color: "#7b889e"; font.pixelSize: 12 } Item { Layout.fillWidth: true } Badge { label: "浅色 · 本版已实现"; fill: "#f3f5f9"; tint: "#9aa6bb" } }
                    Item { Layout.fillHeight: true }
                    Text { text: "偏好与演示数据只保留在当前进程，关闭后恢复样例。"; color: "#afb7c4"; font.pixelSize: 10 }
                }
            }
            Rectangle { Layout.preferredWidth: 310; Layout.fillHeight: true; radius: 14; color: "white"; border.color: "#e5e9f1"
                ColumnLayout { anchors.fill: parent; anchors.margins: 26; spacing: 24
                    Text { text: "关于 TinyIMX"; color: "#68768f"; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Text { text: "C++ 即时通信桌面客户端"; color: "#7e8ba2"; font.pixelSize: 12 }
                    Text { text: "Qt 6.10.2 / Qt Quick / C++\n\n当前版本：0.1.0 · UI 设计原型\n\n已设计会话、好友申请、群管理、文件任务与 AI 助手的完整页面。服务端协议与权限规则见项目设计文档。"; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.7; color: "#a1abbd"; font.pixelSize: 11 }
                    Rectangle { height: 1; Layout.fillWidth: true; color: "#edf0f5" }
                    Text { text: "快捷键"; color: "#7e8ba2"; font.pixelSize: 12 }
                    Repeater { model: ["Ctrl + 1    消息","Ctrl + 2    联系人","Ctrl + 3    群组","Ctrl + 4    文件","Ctrl + 5    AI 助手"]; delegate: Text { required property string modelData; text: modelData; color: "#a1abbd"; font.pixelSize: 11 } }
                    Item { Layout.fillHeight: true }
                    Badge { label: "后端压测已暂停"; fill: "#f2f6f4"; tint: "#92a69c" }
                }
            }
        }
    }
}
