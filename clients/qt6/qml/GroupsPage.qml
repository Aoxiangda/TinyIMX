import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: groups
    required property var shell
    property int selected: 0
    property var group: demo.groups.get(selected)
    Connections { target: demo.groups; function onDataChanged() { groups.group = demo.groups.get(groups.selected) } function onCountChanged() { groups.group = demo.groups.get(groups.selected) } }
    ColumnLayout { anchors.fill: parent; anchors.margins: 32; spacing: 24
        RowLayout { Layout.fillWidth: true; ColumnLayout { spacing: 8; Text { text: "我的群组"; color: "#29364e"; font.pixelSize: 25; font.weight: Font.DemiBold } Text { text: "把成员、权限和讨论放在一个空间"; color: "#909cae"; font.pixelSize: 12 } } Item { Layout.fillWidth: true } UiButton { text: "加入群组"; onClicked: shell.openForm("join","加入群组","输入群 ID；真实接入会校验入群策略与成员上限。") } UiButton { text: "创建群组"; primary: true; iconName: "plus"; onClicked: shell.openForm("group","创建群组","设置群名称与简介，稍后邀请你的伙伴。") } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            Rectangle { Layout.preferredWidth: 310; Layout.fillHeight: true; radius: 14; color: "white"; border.color: "#e5e9f1"
                ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 18
                    Text { text: "已加入的群组 · "+demo.groups.count; color: "#8c98ab"; font.pixelSize: 11 }
                    ListView { id: groupList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: demo.groups; spacing: 10; ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle { required property var item; required property int index; width: groupList.width; height: 104; radius: 10; color: groups.selected===index?"#edf1ff":"#f9fafc"
                            RowLayout { anchors.fill: parent; anchors.margins: 16; spacing: 12; Avatar { width: 46; height: 46; label: item.initial; tint: item.color } ColumnLayout { Layout.fillWidth: true; spacing: 8; Text { text: item.name; color: "#42516c"; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight } Text { text: item.members+" 位成员"; color: "#9aa5b6"; font.pixelSize: 10 } Badge { label: item.state === "disbanded" ? "已解散" : item.role; fill: groups.selected===index?"#e1e8ff":"#eef0f5"; tint: "#8695ba" } } }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: groups.selected=index }
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 14; color: "white"; border.color: "#e5e9f1"
                ScrollView { anchors.fill: parent; anchors.margins: 28; contentWidth: availableWidth
                    ColumnLayout { width: parent.width; spacing: 17
                        RowLayout { Layout.fillWidth: true; Avatar { width: 60; height: 60; radius: 18; label: groups.group.initial || ""; tint: groups.group.color || "#6474df" } ColumnLayout { spacing: 8; Text { text: groups.group.name || ""; color: "#35435e"; font.pixelSize: 22; font.weight: Font.DemiBold } Row { spacing: 10; Badge { label: groups.group.role || "成员" } Text { text: "资料版本 v"+groups.group.version; color: "#a6b0bf"; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter } } } Item { Layout.fillWidth: true } UiButton { text: "群资料"; iconName: "edit"; enabled: groups.group.role === "群主" && groups.group.state === "active"; onClicked: demo.notify("群资料编辑：接入时携带 expected_version，版本冲突后刷新再编辑") } }
                        Text { text: groups.group.description || "暂无介绍"; Layout.fillWidth: true; color: "#8592a6"; font.pixelSize: 13; wrapMode: Text.Wrap }
                        Rectangle { height: 1; Layout.fillWidth: true; color: "#edf0f5" }
                        RowLayout { Layout.fillWidth: true; Text { text: "成员与权限"; color: "#5b6980"; font.pixelSize: 14; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } UiButton { text: "邀请成员"; iconName: "plus"; enabled: groups.group.state === "active" && groups.group.role !== "成员"; onClicked: shell.openForm("invite","邀请成员","输入目标用户 ID；成员权限以服务端结果为准。") } }
                        Repeater { model: [{name:"敖翔",initial:"翔",tint:"#384b69",role:"群主"},{name:"林知夏",initial:"林",tint:"#cda881",role:"管理员"},{name:"陈序",initial:"陈",tint:"#86a49b",role:"成员"}]
                            delegate: RowLayout { required property var modelData; Layout.fillWidth: true; spacing: 14; Avatar { width: 38; height: 38; label: modelData.initial; tint: modelData.tint } Text { text: modelData.name; color: "#6c7990"; font.pixelSize: 12; Layout.fillWidth: true } Badge { label: modelData.role; tint: "#8d9ab3"; fill: "#f2f4f8" } UiButton { text: "权限"; quiet: true; enabled: groups.group.role === "群主" && modelData.name !== "敖翔" && groups.group.state === "active"; onClicked: shell.confirmAction("调整成员权限","为 "+modelData.name+" 调整角色？此为界面预览，真实操作需服务端确认。","role") } }
                        }
                        Text { text: "当前展示 3 位设计样例成员；正式接入按 after_user_id 分页读取。"; color: "#a6b0bf"; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Rectangle { height: 1; Layout.fillWidth: true; color: "#edf0f5" }
                        RowLayout { Layout.fillWidth: true; ColumnLayout { spacing: 6; Text { text: "禁言状态预览"; color: "#6b7890"; font.pixelSize: 13 } Text { text: "切换后可以在聊天页检查输入区的反馈"; color: "#a1acbd"; font.pixelSize: 11 } } Item { Layout.fillWidth: true } Switch { checked: demo.groupMuted; enabled: groups.selected===0 && groups.group.state === "active"; onClicked: demo.groupAction("mute") } }
                        RowLayout { Layout.fillWidth: true; Text { text: "成员上限"; color: "#77849a"; font.pixelSize: 12 } Item { Layout.fillWidth: true } Text { text: "100 人 · 设计示例"; color: "#9ea8b8"; font.pixelSize: 12 } }
                        RowLayout { Layout.fillWidth: true; Text { text: "入群策略"; color: "#77849a"; font.pixelSize: 12 } Item { Layout.fillWidth: true } Badge { label: "开放加入"; fill: "#f3f5f9"; tint: "#9aa6ba" } }
                        Rectangle { height: 1; Layout.fillWidth: true; color: "#edf0f5" }
                        RowLayout { Layout.fillWidth: true; Text { text: "群组操作"; color: "#6f7c92"; font.pixelSize: 12 } Item { Layout.fillWidth: true }
                            UiButton { text: "转让群主"; enabled: groups.selected===0 && groups.group.role === "群主" && groups.group.state === "active"; onClicked: shell.confirmAction("转让群主","将演示群主转让给林知夏？转让后你将不再拥有群主权限。","transfer") }
                            UiButton { text: groups.group.role === "群主" ? "解散群组" : "退出群组"; danger: true; enabled: groups.selected===0 && groups.group.state === "active"; onClicked: shell.confirmAction("确认群组操作","此操作影响成员身份。设计预览将保留历史记录。",groups.group.role === "群主" ? "disband" : "leave") }
                        }
                    }
                }
            }
        }
    }
}
