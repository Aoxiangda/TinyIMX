import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1480; height: 900; minimumWidth: 1080; minimumHeight: 720
    visible: true; title: "TinyIMX · 桌面客户端"
    color: "#f6f7fa"
    property string page: "messages"
    property bool darkMode: false
    property bool enterToSend: true
    property bool notifyEnabled: true
    property string dialogKind: ""
    property string dialogTitle: ""
    property string dialogDescription: ""
    property string pendingAction: ""
    property int pendingIndex: -1
    function openForm(kind, title, description) {
        dialogKind = kind; dialogTitle = title; dialogDescription = description
        formName.text = ""; formNote.text = ""; formDialog.open()
    }
    function confirmAction(title, description, action, index) {
        dialogTitle = title; dialogDescription = description; pendingAction = action
        pendingIndex = index === undefined ? -1 : index; confirmDialog.open()
    }
    function showLogin() { loginDialog.open() }
    function closeLogin() { loginDialog.close() }
    header: Rectangle {
        height: 66; color: "#ffffff"
        Rectangle { height: 1; color: "#e7eaf0"; anchors.bottom: parent.bottom; width: parent.width }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 24; spacing: 14
            Rectangle { width: 34; height: 34; radius: 10; color: "#4c6aeb"; Text { anchors.centerIn: parent; text: "T"; color: "white"; font.pixelSize: 22; font.weight: Font.Bold } }
            Text { text: "TinyIMX"; color: "#202c42"; font.pixelSize: 21; font.weight: Font.DemiBold; font.letterSpacing: 0.3 }
            Rectangle { width: 1; height: 18; color: "#e1e5ec"; Layout.leftMargin: 8; Layout.rightMargin: 4 }
            Text { text: "让沟通，有条不紊"; color: "#929cad"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Badge { label: "设计预览 · 本地数据"; fill: "#f2f4f8"; tint: "#7a8598" }
            Rectangle { color: demo.online ? "#f0f8f4" : "#fff3eb"; radius: 15; implicitWidth: 82; implicitHeight: 28
                Row { anchors.centerIn: parent; spacing: 7; Rectangle { width: 6; height: 6; radius: 3; color: demo.online ? "#63a483" : "#d59656"; anchors.verticalCenter: parent.verticalCenter } Text { text: demo.online ? "演示在线" : "演示离线"; color: demo.online ? "#618975" : "#af7e4d"; font.pixelSize: 11 } }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: demo.online = !demo.online }
                ToolTip.visible: statusHover.hovered; ToolTip.text: "点击切换在线 / 离线，体验失败与重试"; HoverHandler { id: statusHover }
            }
            UiButton { iconName: "bell"; quiet: true; hint: "通知"; onClicked: demo.notify("当前没有新的系统通知") }
            Avatar { width: 32; height: 32; radius: 10; label: "翔"; tint: "#384b69"; MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.showLogin() } }
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 82; color: "#28354d"
            Column { anchors.horizontalCenter: parent.horizontalCenter; y: 20; spacing: 10
                Repeater {
                    model: [{key:"messages",icon:"chat",label:"消息"},{key:"contacts",icon:"users",label:"联系人"},{key:"groups",icon:"group",label:"群组"},{key:"files",icon:"folder",label:"文件"},{key:"ai",icon:"sparkles",label:"AI 助手"}]
                    delegate: Rectangle {
                        required property var modelData
                        width: 64; height: 66; radius: 12; color: root.page === modelData.key ? "#40516d" : navHover.hovered ? "#34435d" : "transparent"
                        Image { source: "qrc:/icons/" + modelData.icon + "-light.svg"; width: 22; height: 22; anchors.horizontalCenter: parent.horizontalCenter; y: 12; opacity: root.page === modelData.key ? 1 : 0.7 }
                        Text { text: modelData.label; color: root.page === modelData.key ? "#ffffff" : "#a5b0c3"; font.pixelSize: 11; anchors.horizontalCenter: parent.horizontalCenter; y: 39 }
                        Rectangle { visible: modelData.key === "messages"; width: 6; height: 6; radius: 3; color: "#a6b5ff"; x: 45; y: 11 }
                        HoverHandler { id: navHover }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.page = modelData.key }
                        Accessible.role: Accessible.Button; Accessible.name: modelData.label
                    }
                }
            }
            Column { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 24; spacing: 17
                UiButton { iconName: "settings-light"; quiet: true; hint: "设置"; onClicked: root.page = "settings" }
                Text { text: "v0.1"; color: "#7f8ba2"; font.pixelSize: 10; anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
        Loader {
            Layout.fillWidth: true; Layout.fillHeight: true
            sourceComponent: root.page === "messages" ? chatComponent : root.page === "contacts" ? contactsComponent : root.page === "groups" ? groupsComponent : root.page === "files" ? filesComponent : root.page === "ai" ? aiComponent : settingsComponent
        }
    }
    Component { id: chatComponent; ChatPage { shell: root } }
    Component { id: contactsComponent; ContactsPage { shell: root } }
    Component { id: groupsComponent; GroupsPage { shell: root } }
    Component { id: filesComponent; FilesPage { shell: root } }
    Component { id: aiComponent; AIPage { shell: root } }
    Component { id: settingsComponent; SettingsPage { shell: root } }
    Connections { target: demo; function onNoticeChanged() { toastText.text = demo.notice; toast.visible = true; toastTimer.restart() } }
    Rectangle {
        id: toast; visible: false; z: 80; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 25
        width: Math.min(toastText.implicitWidth + 40, root.width - 120); height: toastText.implicitHeight + 26; radius: 10; color: "#34435a"
        Text { id: toastText; anchors.fill: parent; anchors.margins: 13; color: "white"; font.pixelSize: 12; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
        Timer { id: toastTimer; interval: 4800; onTriggered: toast.visible = false }
    }
    Dialog {
        id: formDialog; modal: true; anchors.centerIn: parent; width: 470; padding: 28; title: root.dialogTitle
        background: Rectangle { color: "white"; radius: 16; border.color: "#dfe4ed" }
        contentItem: ColumnLayout { spacing: 16
            Text { Layout.fillWidth: true; text: root.dialogDescription; wrapMode: Text.Wrap; color: "#7b879a"; font.pixelSize: 13 }
            TextField { id: formName; Layout.fillWidth: true; placeholderText: root.dialogKind === "group" ? "群名称" : root.dialogKind === "join" ? "群 ID" : "目标用户 ID"; maximumLength: 60; selectByMouse: true }
            TextArea { id: formNote; Layout.fillWidth: true; Layout.preferredHeight: 90; placeholderText: root.dialogKind === "friend" ? "添加申请说明" : "群介绍（可选）"; wrapMode: TextEdit.Wrap; selectByMouse: true; background: Rectangle { radius: 8; color: "#f6f7fa"; border.color: "#e1e5ed" } }
            RowLayout { Item { Layout.fillWidth: true } UiButton { text: "取消"; onClicked: formDialog.close() } UiButton { text: root.dialogKind === "group" ? "创建群组" : root.dialogKind === "join" ? "申请加入" : "发送申请"; primary: true; enabled: formName.text.trim().length > 0; onClicked: { if(root.dialogKind === "friend")demo.addFriend(formName.text,formNote.text);else if(root.dialogKind === "group")demo.createGroup(formName.text,formNote.text);else if(root.dialogKind === "invite")demo.groupAction("invite");else demo.notify("已预览加入群组请求；真实加入结果需服务端确认");formDialog.close() } } }
        }
    }
    Dialog {
        id: confirmDialog; modal: true; anchors.centerIn: parent; width: 460; title: root.dialogTitle; padding: 28
        background: Rectangle { color: "white"; radius: 16; border.color: "#dfe4ed" }
        contentItem: ColumnLayout { spacing: 24
            Text { text: root.dialogDescription; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#6d7a90"; font.pixelSize: 14 }
            RowLayout { Item { Layout.fillWidth: true } UiButton { text: "返回"; onClicked: confirmDialog.close() } UiButton { text: "确认"; danger: true; onClicked: { if(root.pendingAction === "cancel-transfer")demo.transferAction(root.pendingIndex,"cancel");else if(root.pendingAction === "reject-friend")demo.acceptRequest(root.pendingIndex,false);else demo.groupAction(root.pendingAction);confirmDialog.close() } } }
        }
    }
    Dialog {
        id: loginDialog; modal: true; anchors.centerIn: parent; width: 490; padding: 36; closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: "white"; radius: 20; border.color: "#e1e5ed" }
        contentItem: ColumnLayout { spacing: 18
            Rectangle { width: 50; height: 50; radius: 15; color: "#4c6aeb"; Layout.alignment: Qt.AlignHCenter; Text { text: "T"; color: "white"; font.pixelSize: 30; font.weight: Font.Bold; anchors.centerIn: parent } }
            Text { text: "欢迎回到 TinyIMX"; font.pixelSize: 24; font.weight: Font.DemiBold; color: "#26334c"; Layout.alignment: Qt.AlignHCenter }
            Text { text: "连接你的伙伴，继续上一次的讨论"; color: "#8a95a7"; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter }
            TextField { id: loginUser; Layout.fillWidth: true; placeholderText: "用户名"; selectByMouse: true }
            TextField { id: loginPassword; Layout.fillWidth: true; placeholderText: "密码"; echoMode: TextInput.Password; onAccepted: if(loginUser.text.trim() && loginPassword.text)demo.notify("当前为设计原型，不会发送或保存账号密码") }
            Text { text: "服务地址"; color: "#6e7b8f"; font.pixelSize: 12 }
            TextField { Layout.fillWidth: true; text: "192.168.220.128:9000"; selectByMouse: true }
            UiButton { Layout.fillWidth: true; text: "登录（待接入）"; enabled: false; hint: "账号认证尚未接入；密码不会保存或发送" }
            UiButton { Layout.fillWidth: true; text: "进入设计预览"; primary: true; onClicked: { loginPassword.text="";loginDialog.close() } }
            Text { text: "此版本使用本地示例数据，不会向服务器发起登录。"; color: "#a0a9b8"; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
        }
        onClosed: loginPassword.text = ""
    }
    Shortcut { sequence: "Ctrl+1"; onActivated: root.page="messages" }
    Shortcut { sequence: "Ctrl+2"; onActivated: root.page="contacts" }
    Shortcut { sequence: "Ctrl+3"; onActivated: root.page="groups" }
    Shortcut { sequence: "Ctrl+4"; onActivated: root.page="files" }
    Shortcut { sequence: "Ctrl+5"; onActivated: root.page="ai" }
}
