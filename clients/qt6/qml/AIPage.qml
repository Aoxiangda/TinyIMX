import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: ai
    required property var shell
    ColumnLayout { anchors.fill: parent; anchors.margins: 32; spacing: 24
        RowLayout { Layout.fillWidth: true; ColumnLayout { spacing: 8; Text { text: "AI 助手"; color: "#29364e"; font.pixelSize: 25; font.weight: Font.DemiBold } Text { text: "整理思路，把讨论变成可以行动的下一步"; color: "#909cae"; font.pixelSize: 12 } } Item { Layout.fillWidth: true } Badge { label: "Ollama · 接口待接入"; fill: "#eeedf9"; tint: "#a298bb" } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "white"; radius: 14; border.color: "#e5e9f1"
                ColumnLayout { anchors.fill: parent; anchors.margins: 30; spacing: 20
                    ColumnLayout { visible: demo.aiState === "idle"; Layout.fillWidth: true; spacing: 16; Layout.topMargin: 40
                        Rectangle { width: 68; height: 68; radius: 20; color: "#f0edfb"; Layout.alignment: Qt.AlignHCenter; Image { source: "qrc:/icons/sparkles.svg"; width: 30; height: 30; anchors.centerIn: parent } }
                        Text { text: "今天有什么可以帮你？"; color: "#53607a"; font.pixelSize: 23; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                        Text { text: "从一段讨论、一份文档或一个问题开始"; color: "#a1abbd"; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter }
                        Flow { Layout.fillWidth: true; spacing: 12; Layout.topMargin: 12
                            Repeater { model: ["整理项目的待办清单","解释这段接口设计","梳理群聊中的关键结论"]
                                delegate: UiButton { required property string modelData; text: modelData; onClicked: prompt.text=modelData }
                            }
                        }
                    }
                    RowLayout { visible: demo.aiState === "thinking"; Layout.fillWidth: true; spacing: 12; BusyIndicator { running: visible; implicitWidth: 30; implicitHeight: 30 } Text { text: "正在生成本地示例回复…"; color: "#9c92b7"; font.pixelSize: 13 } Item { Layout.fillWidth: true } UiButton { text: "停止生成"; onClicked: demo.cancelAI() } }
                    ScrollView { Layout.fillWidth: true; Layout.fillHeight: true
                        TextArea { readOnly: true; selectByMouse: true; text: demo.aiAnswer; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; padding: 8; color: demo.aiState === "error" ? "#b9847c" : "#74829a"; font.pixelSize: 13; background: Item {} }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 148; radius: 12; color: "#f8f9fc"; border.color: "#e6e9f2"
                        ColumnLayout { anchors.fill: parent; anchors.margins: 15; spacing: 12
                            ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; TextArea { id: prompt; placeholderText: "输入你的问题…"; selectByMouse: true; wrapMode: TextEdit.Wrap; color: "#55627b"; font.pixelSize: 13; background: Item {} } }
                            RowLayout { Layout.fillWidth: true; Text { text: "输入仅留在本地预览中"; color: "#a9b1c0"; font.pixelSize: 10 } Item { Layout.fillWidth: true } UiButton { text: demo.aiState === "error" ? "重试" : "发送"; primary: true; enabled: prompt.text.trim().length>0 && demo.aiState !== "thinking"; onClicked: demo.askAI(prompt.text) } }
                        }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 274; Layout.fillHeight: true; color: "white"; radius: 14; border.color: "#e5e9f1"
                ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 24
                    Text { text: "上下文与工具"; color: "#6d7992"; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Text { text: "先选择需要的上下文，再让助手参与。"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#a0aaba"; font.pixelSize: 11; lineHeight: 1.5 }
                    Repeater { model: [{title:"会话记录",note:"接入后，读取当前用户有权访问的消息"},{title:"联系人与群组",note:"查看已授权的联系人与成员资料"},{title:"文件元信息",note:"查看授权范围内的文件信息"}]
                        delegate: ColumnLayout { required property var modelData; Layout.fillWidth: true; spacing: 9; RowLayout { Text { text: modelData.title; color: "#7a87a0"; font.pixelSize: 12 } Item { Layout.fillWidth: true } Badge { label: "待接入"; fill: "#f3f4f8"; tint: "#a8b1c1" } } Text { text: modelData.note; color: "#a8b0c0"; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.6 } }
                    }
                    Item { Layout.fillHeight: true }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 116; radius: 10; color: "#f6f4fb"; Text { anchors.fill: parent; anchors.margins: 14; text: "服务状态\n\n当前为本地示例，不会调用模型。正式接入时显示连接失败、模型加载中、生成中与工具调用结果。"; color: "#a198b7"; font.pixelSize: 11; wrapMode: Text.Wrap; lineHeight: 1.4 } }
                }
            }
        }
    }
}
