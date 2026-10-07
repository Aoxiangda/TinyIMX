import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: contacts
    required property var shell
    property int selected: 0
    property var person: demo.contacts.get(selected)
    ColumnLayout { anchors.fill: parent; anchors.margins: 32; spacing: 22
        RowLayout { Layout.fillWidth: true
            ColumnLayout { spacing: 8; Text { text: "联系人"; font.pixelSize: 25; font.weight: Font.DemiBold; color: "#29364e" } Text { text: "找到伙伴，让下一次沟通更简单"; font.pixelSize: 12; color: "#909cae" } }
            Item { Layout.fillWidth: true } UiButton { text: "添加联系人"; iconName: "plus"; primary: true; onClicked: shell.openForm("friend","添加联系人","使用用户 ID 发送好友申请，对方接受后即可开始私聊。") }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: requestsContent.implicitHeight+36; color: "white"; radius: 14; border.color: "#e5e9f1"
            ColumnLayout { id: requestsContent; anchors.fill: parent; anchors.margins: 18; spacing: 12
                RowLayout { Text { text: "好友申请"; color: "#4d5a71"; font.pixelSize: 14; font.weight: Font.DemiBold } Badge { label: ""+demo.requests.count; fill: "#f0f3f8"; tint: "#8a96aa" } Item { Layout.fillWidth: true } Text { text: "申请与联系人分开管理"; color: "#a3adbd"; font.pixelSize: 11 } }
                Repeater { model: demo.requests
                    delegate: RowLayout { required property var item; required property int index; Layout.fillWidth: true; spacing: 14
                        Avatar { width: 38; height: 38; label: item.name.substring(0,1); tint: "#a09cc2" }
                        ColumnLayout { Layout.fillWidth: true; spacing: 5; Text { text: item.name; color: "#4e5b72"; font.pixelSize: 12 } Text { text: item.note; color: "#939eaf"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true } }
                        UiButton { visible: item.state === "pending"; text: "拒绝"; quiet: true; onClicked: shell.confirmAction("拒绝好友申请","拒绝来自 "+item.name+" 的申请？", "reject-friend",index) }
                        UiButton { visible: item.state === "pending"; text: "接受"; onClicked: demo.acceptRequest(index,true) }
                        Badge { visible: item.state !== "pending"; label: item.state === "accepted" ? "已接受" : item.state === "outgoing" ? "等待对方确认" : "已拒绝"; tint: "#8d98aa"; fill: "#f1f3f7" }
                    }
                }
            }
        }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 22
            Rectangle { Layout.preferredWidth: 350; Layout.fillHeight: true; color: "white"; radius: 14; border.color: "#e5e9f1"
                ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 16
                    TextField { id: filter; Layout.fillWidth: true; placeholderText: "搜索已添加的联系人"; font.pixelSize: 12; selectByMouse: true; background: Rectangle { radius: 8; color: "#f2f4f8" } }
                    Text { text: "我的联系人 · "+demo.contacts.count; color: "#919eb0"; font.pixelSize: 11 }
                    ListView { id: people; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: demo.contacts; spacing: 5; ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle { required property var item; required property int index; property bool match: filter.text === "" || item.name.indexOf(filter.text)>=0; width: people.width; height: match?72:0; visible: match; radius: 10; color: contacts.selected===index?"#edf2ff":"transparent"
                            RowLayout { anchors.fill: parent; anchors.margins: 12; spacing: 12; Avatar { width: 40; height: 40; label: item.initial; tint: item.color; showStatus: true; online: item.online } ColumnLayout { spacing: 6; Text { text: item.name; color: "#4e5b72"; font.pixelSize: 13 } Text { text: item.subtitle; color: "#9ca6b6"; font.pixelSize: 11 } } }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: contacts.selected=index }
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "white"; radius: 14; border.color: "#e5e9f1"
                ColumnLayout { anchors.horizontalCenter: parent.horizontalCenter; y: 52; width: Math.min(parent.width-80,420); spacing: 20
                    Avatar { Layout.preferredWidth: 86; Layout.preferredHeight: 86; radius: 26; Layout.alignment: Qt.AlignHCenter; label: contacts.person.initial || ""; tint: contacts.person.color || "#9999bb"; showStatus: true; online: contacts.person.online || false }
                    Text { text: contacts.person.name || ""; font.pixelSize: 25; color: "#38455e"; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                    Badge { label: contacts.person.online ? "在线 · 设计样例" : "离线 · 设计样例"; fill: "#f2f6f5"; tint: "#8a9b94"; Layout.alignment: Qt.AlignHCenter }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5"; Layout.topMargin: 10 }
                    RowLayout { Layout.fillWidth: true; Text { text: "用户 ID"; color: "#9ba5b5"; font.pixelSize: 12 } Item { Layout.fillWidth: true } Text { text: contacts.person.id || ""; color: "#7a879c"; font.pixelSize: 12 } }
                    RowLayout { Layout.fillWidth: true; Text { text: "备注"; color: "#9ba5b5"; font.pixelSize: 12 } Item { Layout.fillWidth: true } Text { text: contacts.person.subtitle || ""; color: "#7a879c"; font.pixelSize: 12 } }
                    UiButton { text: "发送消息"; primary: true; Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 20; onClicked: { var found=false; for(var i=0;i<demo.conversations.count;i++){if(demo.conversations.get(i).id===contacts.person.id){demo.selectConversation(i);found=true;break}} if(found)shell.page="messages";else demo.notify("该新联系人尚无演示会话；真实接入将创建或读取私聊会话") } }
                    Text { text: "好友关系由双方申请与确认建立。"; color: "#aeb6c3"; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                }
            }
        }
    }
}
