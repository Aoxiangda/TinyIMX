import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: page
    required property var shell
    objectName: "liveGroupsPage"
    Component.onCompleted: { if(liveSession.groupInfo.id)select(liveSession.groupInfo.id) }
    property string selectedId: ""
    property var info: liveSession.groupInfo
    property bool owner: info.owner_user_id === liveSession.accountId
    property bool manage: owner || info.role === "admin"
    property string action: "invite"
    property string target: ""
    function select(id) { selectedId=id; liveSession.selectGroup(id) }
    ColumnLayout { anchors.fill: parent; anchors.margins: 28; spacing: 20
        RowLayout { Layout.fillWidth: true
            ColumnLayout { Text { text: "群组与成员"; font.pixelSize: 25; color: "#29364e" } Text { text: "群资料、权限和发送结果由服务器确认"; color: "#8592a6"; font.pixelSize: 12 } }
            Item { Layout.fillWidth: true }
            UiButton { text: "刷新"; onClicked: { liveSession.refresh(); if(selectedId)liveSession.selectGroup(selectedId) } }
            UiButton { text: "加入群组"; onClicked: shell.openForm("join","加入群组","输入开放群组 ID；受入群策略和容量限制。") }
            UiButton { text: "创建群组"; primary: true; onClicked: shell.openForm("group","创建群组","输入群名称与介绍，创建后可邀请成员。") }
        }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 20
            Rectangle { Layout.preferredWidth: 290; Layout.fillHeight: true; color: "white"; radius: 14
                ListView { id: list; anchors.fill: parent; anchors.margins: 16; clip: true; model: liveSession.groups; spacing: 10; ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle { required property var item; width: list.width; height: 82; radius: 10; color: selectedId===item.id?"#edf1ff":"#f8f9fc"
                        Column { anchors.fill: parent; anchors.margins: 16; spacing: 8; Text { text: item.name; width: parent.width; elide: Text.ElideRight; color: "#42516c"; font.pixelSize: 14 } Text { text: "ID "+item.id+" · "+(item.status==="active"?"正常":"已解散"); color: "#919bb0"; font.pixelSize: 11 } }
                        MouseArea { anchors.fill: parent; onClicked: page.select(item.id) }
                    }
                    Text { visible: list.count===0; anchors.centerIn: parent; text: "尚未加入群组"; color: "#909caf" }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "white"; radius: 14
                ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 16; visible: selectedId.length>0
                    RowLayout { Layout.fillWidth: true; Text { text: info.name || "正在读取群资料…"; font.pixelSize: 22; color: "#35435e"; Layout.fillWidth: true } Badge { label: page.owner?"群主":info.role==="admin"?"管理员":"成员" } UiButton { text: "进入群聊"; primary: true; enabled: info.status==="active"; onClicked: { liveSession.openGroup(selectedId);shell.page="messages" } } }
                    Text { text: info.description || "暂无介绍"; color: "#8592a6"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Text { text: "群 ID："+selectedId+" · 版本 "+(info.version || "—")+" · 上限 "+(info.max_members || "—")+" · "+(info.join_policy==="open"?"开放加入":"邀请加入"); color: "#929eb0"; font.pixelSize: 11 }
                    RowLayout { Layout.fillWidth: true; Text { text: "成员 · "+liveSession.groupMembers.count; color: "#5b6980" } Item { Layout.fillWidth: true } UiButton { text: "邀请成员"; enabled: page.manage; onClicked: { page.action="invite";page.target="";memberDialog.open() } } UiButton { text: "编辑资料"; enabled: page.owner; onClicked: { editName.text=info.name;editDescription.text=info.description;editDialog.open() } } }
                    ListView { id: members; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: liveSession.groupMembers; spacing: 8; ScrollBar.vertical: ScrollBar {}
                        delegate: RowLayout { required property var item; width: members.width; height: 54
                            Avatar { width: 36; height: 36; label: item.id===liveSession.accountId?"我":"友"; tint: "#86a49b" }
                            Text { text: "用户 "+item.id+(item.id===liveSession.accountId?"（我）":""); Layout.fillWidth: true; color: "#6c7990" }
                            Badge { label: item.role==="owner"?"群主":item.role==="admin"?"管理员":"成员" }
                            Text { visible: item.muted_until.length>0; text: "禁言至 "+item.muted_until; font.pixelSize: 10; color: "#b78b5c" }
                            UiButton { text: "管理"; enabled: page.manage && item.id!==liveSession.accountId && item.role!=="owner"; onClicked: { page.target=item.id;page.action="mute";memberDialog.open() } }
                        }
                    }
                    RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } UiButton { text: "转让群主"; enabled: page.owner; onClicked: { page.action="transfer";page.target="";memberDialog.open() } } UiButton { text: page.owner?"解散群组":"退出群组"; danger: true; enabled: info.status==="active"; onClicked: leaveDialog.open() } }
                }
                Text { visible: selectedId.length===0; anchors.centerIn: parent; text: "选择群组查看资料与成员"; color: "#929caf" }
            }
        }
    }
    Dialog { id: memberDialog; anchors.centerIn: parent; modal: true; width: 440; title: "成员操作"
        contentItem: ColumnLayout { spacing: 16
            TextField { id: memberId; Layout.fillWidth: true; text: page.target; placeholderText: "目标用户 ID"; selectByMouse: true }
            ComboBox { id: operation; Layout.fillWidth: true; model: ["邀请加入","移出群组","设为管理员","设为普通成员","禁言 10 分钟","解除禁言","转让群主"]; currentIndex: ["invite","kick","admin","member","mute","unmute","transfer"].indexOf(page.action) }
            Text { text: "权限与成员状态由服务器校验；转让后你会失去群主权限。"; color: "#8995a8"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout { Item { Layout.fillWidth: true } UiButton { text: "取消"; onClicked: memberDialog.close() } UiButton { text: "确认操作"; primary: true; enabled: memberId.text.trim().length>0; onClicked: { liveSession.mutateGroup(["invite","kick","admin","member","mute","unmute","transfer"][operation.currentIndex],selectedId,memberId.text.trim());memberDialog.close() } } }
        }
    }
    Dialog { id: editDialog; anchors.centerIn: parent; modal: true; width: 460; title: "编辑群资料"
        contentItem: ColumnLayout { TextField { id: editName; Layout.fillWidth: true; placeholderText: "群名称" } TextArea { id: editDescription; Layout.fillWidth: true; placeholderText: "群介绍"; wrapMode: TextEdit.Wrap } UiButton { text: "保存"; primary: true; onClicked: { liveSession.mutateGroup("update",selectedId,editName.text,editDescription.text);editDialog.close() } } }
    }
    Dialog { id: leaveDialog; anchors.centerIn: parent; modal: true; title: page.owner?"确认解散群组":"确认退出群组"
        contentItem: ColumnLayout { Text { text: page.owner?"解散后成员不能继续发送消息。":"退出后将失去群成员权限。"; color: "#6c7990" } RowLayout { UiButton { text: "取消"; onClicked: leaveDialog.close() } UiButton { text: "确认"; danger: true; onClicked: { liveSession.mutateGroup(page.owner?"disband":"leave",selectedId);leaveDialog.close() } } } }
    }
}
