import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: chat
    required property var shell
    property bool showInspector: true
    property int selectedIndex: demo.selectedIndex
    property var current: demo.current
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.preferredWidth: chat.width < 1100 ? 276 : 302; Layout.fillHeight: true; color: "#fbfcfe"
            Rectangle { width: 1; height: parent.height; color: "#e7eaf1"; anchors.right: parent.right }
            ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 16
                RowLayout { Layout.fillWidth: true; Text { text: "消息"; font.pixelSize: 23; font.weight: Font.DemiBold; color: "#26334a" } Item { Layout.fillWidth: true } UiButton { iconName: "plus"; quiet: true; hint: "新建群聊"; onClicked: shell.openForm("group","创建群组","为新的讨论建立一个空间。") } }
                TextField { id: search; Layout.fillWidth: true; placeholderText: "搜索会话或联系人"; leftPadding: 35; font.pixelSize: 12; selectByMouse: true; background: Rectangle { radius: 8; color: "#eef1f6" } Image { source: "qrc:/icons/search.svg"; width: 16; height: 16; x: 11; anchors.verticalCenter: parent.verticalCenter } }
                RowLayout { Layout.fillWidth: true; Text { text: "全部会话"; color: "#758197"; font.pixelSize: 11 } Item { Layout.fillWidth: true } Text { text: ""+demo.conversations.count; color: "#a0aaba"; font.pixelSize: 11 } }
                ListView {
                    id: conversationList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5; model: demo.conversations
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: conversation; required property var item; required property int index
                        property bool matches: search.text === "" || item.name.indexOf(search.text) >= 0 || item.preview.indexOf(search.text)>=0
                        width: conversationList.width; height: matches ? 82 : 0; visible: matches; radius: 10
                        color: chat.selectedIndex === index ? "#eaf0ff" : convoHover.hovered ? "#f0f3f8" : "transparent"
                        Rectangle { visible: chat.selectedIndex === index; width: 3; height: 32; radius: 2; color: "#4c6aeb"; y: 25 }
                        RowLayout { anchors.fill: parent; anchors.margins: 12; spacing: 10
                            Avatar { label: item.initial; tint: item.color; showStatus: item.kind === "private"; online: item.online; Layout.preferredWidth: 42; Layout.preferredHeight: 42 }
                            ColumnLayout { Layout.fillWidth: true; spacing: 7
                                RowLayout { Layout.fillWidth: true; Text { text: item.name; color: "#334059"; font.pixelSize: 13; font.weight: chat.selectedIndex===index?Font.DemiBold:Font.Normal; Layout.fillWidth: true; elide: Text.ElideRight } Text { text: item.time; font.pixelSize: 10; color: "#a0aaba" } }
                                RowLayout { Layout.fillWidth: true; Text { text: item.preview; color: "#8994a6"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight } Rectangle { visible: item.unread>0; radius: 8; color: "#748bec"; width: 18; height: 18; Text { anchors.centerIn: parent; text: item.unread; color: "white"; font.pixelSize: 10 } } }
                            }
                        }
                        HoverHandler { id: convoHover }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: demo.selectConversation(index) }
                    }
                    Text { anchors.centerIn: parent; visible: search.text!=="" && conversationList.contentHeight===0; text: "没有找到相关会话"; color: "#99a3b4"; font.pixelSize: 12 }
                }
                RowLayout { spacing: 6; Rectangle { width: 5; height: 5; radius: 3; color: "#b7bfcd" } Text { text: "消息记录 · 本地设计样例"; color: "#a0a9b8"; font.pixelSize: 10 } }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 84; color: "#ffffff"
                Rectangle { height: 1; width: parent.width; color: "#e8ecf2"; anchors.bottom: parent.bottom }
                RowLayout { anchors.fill: parent; anchors.margins: 22; spacing: 12
                    Avatar { width: 40; height: 40; label: chat.current.initial || ""; tint: chat.current.color || "#6474df" }
                    ColumnLayout { Layout.fillWidth: true; spacing: 5
                        Text { text: chat.current.name || ""; Layout.fillWidth: true; font.pixelSize: 17; font.weight: Font.DemiBold; color: "#2d3a52" }
                        Text { text: chat.current.subtitle || ""; Layout.fillWidth: true; font.pixelSize: 11; color: "#94a0b2" }
                    }
                    UiButton { iconName: "search"; quiet: true; hint: "搜索当前会话（本地样例）"; onClicked: historySearch.visible = !historySearch.visible }
                    UiButton { iconName: "panel"; quiet: true; hint: "展开 / 收起会话资料"; onClicked: chat.showInspector = !chat.showInspector }
                }
            }
            TextField { id: historySearch; visible: false; Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; placeholderText: "搜索当前已加载消息"; selectByMouse: true }
            Rectangle { visible: !demo.online || (demo.groupMuted && chat.current.kind === "group"); Layout.fillWidth: true; Layout.preferredHeight: 36; color: "#fff5e9"
                Text { anchors.centerIn: parent; text: !demo.online ? "当前离线 · 草稿保留，发送失败后可手动重试" : "你已被禁言 · 解除后可以继续发送"; color: "#ad865a"; font.pixelSize: 11 }
            }
            ListView {
                id: messageList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 18; model: demo.messages; cacheBuffer: 300
                topMargin: 15; bottomMargin: 16; ScrollBar.vertical: ScrollBar {}
                header: Item { width: messageList.width; height: 35; UiButton { anchors.centerIn: parent; text: "查看更早消息"; quiet: true; onClicked: demo.loadEarlier() } }
                delegate: Item {
                    id: row; required property var item; required property int index
                    property bool separator: item.kind === "separator"
                    property bool matches: historySearch.text === "" || item.text.indexOf(historySearch.text)>=0
                    width: messageList.width; height: !matches ? 0 : separator ? 28 : bubbleColumn.implicitHeight + 6; visible: matches
                    Text { visible: row.separator; text: row.item.text; anchors.horizontalCenter: parent.horizontalCenter; color: "#a1abbc"; font.pixelSize: 10; y: 4 }
                    Avatar { visible: !row.separator; width: 34; height: 34; radius: 10; label: row.item.own ? "翔" : row.item.author.substring(0,1); tint: row.item.own ? "#384b69" : row.item.author === "陈序" ? "#86a49b" : "#cda881"; x: row.item.own ? parent.width-width-24 : 24; y: 3 }
                    ColumnLayout {
                        id: bubbleColumn; visible: !row.separator; spacing: 6
                        width: row.item.kind === "file" ? Math.min(300,messageList.width-150) : Math.min(messageText.implicitWidth + 32,messageList.width*0.76-55)
                        x: row.item.own ? row.width-width-70 : 70
                        Text { text: row.item.own ? "我" : row.item.author; font.pixelSize: 10; color: "#8b96a8"; Layout.alignment: row.item.own?Qt.AlignRight:Qt.AlignLeft }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: row.item.kind === "file" ? 92 : messageText.implicitHeight+24; radius: 12
                            color: row.item.own ? "#4c6aeb" : "#ffffff"; border.width: row.item.own ? 0 : 1; border.color: "#e7ebf2"
                            Text { id: messageText; visible: row.item.kind !== "file"; text: row.item.text; textFormat: Text.PlainText; color: row.item.own ? "#ffffff" : "#4e5b70"; font.pixelSize: 13; lineHeight: 1.4; width: parent.width-30; anchors.centerIn: parent; wrapMode: Text.Wrap }
                            RowLayout { visible: row.item.kind === "file"; anchors.fill: parent; anchors.margins: 16; spacing: 12
                                Rectangle { width: 42; height: 48; radius: 7; color: "#fff0ed"; Text { text: "PDF"; anchors.centerIn: parent; color: "#c9897c"; font.pixelSize: 10; font.weight: Font.Bold } }
                                ColumnLayout { Layout.fillWidth: true; spacing: 7; Text { text: row.item.text; Layout.fillWidth: true; elide: Text.ElideRight; color: "#4c596f"; font.pixelSize: 12 } Text { text: "2.4 MB · 示例附件"; color: "#9ba4b3"; font.pixelSize: 10 } Text { text: "在文件中心查看  ↗"; color: "#6b80d9"; font.pixelSize: 10 } }
                            }
                            MouseArea { visible: row.item.kind === "file"; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: shell.page="files" }
                        }
                        RowLayout { Layout.alignment: row.item.own ? Qt.AlignRight : Qt.AlignLeft; spacing: 8
                            Text { text: row.item.time; font.pixelSize: 9; color: "#a0aaba" }
                            Text { visible: row.item.own; text: row.item.status === "failed" ? "发送失败" : "已保存 · 演示"; color: row.item.status === "failed" ? "#c57469" : "#8b99b1"; font.pixelSize: 9 }
                            UiButton { visible: row.item.status === "failed"; text: "重试"; implicitHeight: 24; quiet: true; onClicked: demo.retryMessage(row.index) }
                        }
                    }
                }
                onCountChanged: Qt.callLater(function(){messageList.positionViewAtEnd()})
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 164; color: "#ffffff"
                Rectangle { width: parent.width; height: 1; color: "#e7eaf0" }
                ColumnLayout { anchors.fill: parent; anchors.margins: 18; anchors.topMargin: 12; spacing: 5
                    RowLayout { spacing: 3
                        UiButton { iconName: "smile"; quiet: true; hint: "表情"; implicitHeight: 30; onClicked: emojiMenu.open() }
                        UiButton { iconName: "attach"; quiet: true; hint: "附件"; implicitHeight: 30; onClicked: demo.chooseFile() }
                        UiButton { iconName: "history"; quiet: true; hint: "历史消息"; implicitHeight: 30; onClicked: demo.loadEarlier() }
                        Item { Layout.fillWidth: true }
                        Text { text: "文本消息"; color: "#b1b8c5"; font.pixelSize: 10 }
                    }
                    ScrollView { Layout.fillWidth: true; Layout.fillHeight: true
                        TextArea { id: editor; objectName: "messageEditor"; text: demo.draft; onTextChanged: demo.draft = text; placeholderText: demo.groupMuted && chat.current.kind === "group" ? "你已被禁言" : "输入消息，开始新的讨论…"; enabled: !(demo.groupMuted && chat.current.kind === "group"); wrapMode: TextEdit.Wrap; selectByMouse: true; padding: 2; font.pixelSize: 13; color: "#3c4860"; background: Item {}
                            Keys.onReturnPressed: function(event) {
                                if(shell.enterToSend && !(event.modifiers & Qt.ShiftModifier) && !event.isAutoRepeat && editor.preeditText.length===0) { if(demo.sendMessage(text))text="";event.accepted=true }
                            }
                        }
                    }
                    RowLayout { Layout.fillWidth: true; Text { text: shell.enterToSend ? "Enter 发送 · Shift + Enter 换行" : "Ctrl + Enter 发送"; color: "#a7b0bf"; font.pixelSize: 10 } Item { Layout.fillWidth: true } UiButton { text: "发送"; primary: true; implicitWidth: 76; implicitHeight: 32; enabled: editor.enabled && editor.text.trim().length>0; onClicked: { if(demo.sendMessage(editor.text))editor.text="" } } }
                }
                Menu { id: emojiMenu; y: -80; MenuItem { text: "🙂  微笑"; onTriggered: editor.insert(editor.cursorPosition,"🙂") } MenuItem { text: "👍  赞同"; onTriggered: editor.insert(editor.cursorPosition,"👍") } MenuItem { text: "🎉  庆祝"; onTriggered: editor.insert(editor.cursorPosition,"🎉") } }
                Shortcut { sequence: "Ctrl+Return"; autoRepeat: false; onActivated: if(editor.preeditText.length===0 && demo.sendMessage(editor.text))editor.text="" }
            }
        }
        Rectangle {
            visible: chat.showInspector && chat.width>=1200; Layout.preferredWidth: 270; Layout.fillHeight: true; color: "#ffffff"
            Rectangle { width: 1; height: parent.height; color: "#e8ecf2" }
            ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 20
                RowLayout { Text { text: "会话资料"; font.pixelSize: 13; color: "#6c7990" } Item { Layout.fillWidth: true } UiButton { iconName: "close"; quiet: true; hint: "收起资料"; onClicked: chat.showInspector=false } }
                Avatar { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 64; Layout.preferredHeight: 64; radius: 19; label: chat.current.initial || ""; tint: chat.current.color || "#6474df" }
                ColumnLayout { Layout.fillWidth: true; spacing: 8; Text { text: chat.current.name || ""; color: "#35425a"; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter } Badge { Layout.alignment: Qt.AlignHCenter; label: chat.current.kind === "group" ? "项目协作" : "联系人"; fill: "#f0f2f8"; tint: "#8b96a9" } }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                ColumnLayout { Layout.fillWidth: true; spacing: 10; Text { text: chat.current.kind === "group" ? "群公告" : "个人简介"; color: "#758297"; font.pixelSize: 11; font.weight: Font.DemiBold } Text { Layout.fillWidth: true; text: chat.current.kind === "group" ? "一起把产品做好。\n接口、设计、开发与测试在这里协作。" : "保持沟通，一起做有意思的项目。"; color: "#96a0b1"; font.pixelSize: 11; lineHeight: 1.6; wrapMode: Text.Wrap } }
                ColumnLayout { visible: chat.current.kind === "group"; Layout.fillWidth: true; spacing: 14
                    RowLayout { Text { text: "群成员"; color: "#758297"; font.pixelSize: 11; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } Text { text: "查看全部  ›"; color: "#a0a9b8"; font.pixelSize: 10; MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: shell.page="groups" } } }
                    Row { spacing: 11; Avatar { width: 38; height: 38; label: "翔"; tint: "#384b69" } Avatar { width: 38; height: 38; label: "林"; tint: "#cda881" } Avatar { width: 38; height: 38; label: "陈"; tint: "#86a49b" } Avatar { width: 38; height: 38; label: "+9"; tint: "#aab2c9" } }
                    UiButton { Layout.fillWidth: true; text: "邀请成员"; iconName: "plus"; onClicked: shell.openForm("invite","邀请成员","选择或输入成员 ID；真实接入后由群权限校验。") }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                UiButton { text: "共享文件"; iconName: "folder"; quiet: true; onClicked: shell.page="files" }
                UiButton { visible: chat.current.kind === "group"; text: "管理群组"; iconName: "settings"; quiet: true; onClicked: shell.page="groups" }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; implicitHeight: 68; radius: 10; color: "#f7f8fc"; Text { anchors.fill: parent; anchors.margins: 12; text: "资料与成员信息为本地设计样例。\n真实数据由服务端授权后提供。"; color: "#a1aabb"; font.pixelSize: 10; wrapMode: Text.Wrap; lineHeight: 1.5 } }
            }
        }
    }
}
