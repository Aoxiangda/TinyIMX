import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: files
    required property var shell
    property string filter: "all"
    function stateLabel(state) { return ({completed:"已完成",paused:"已暂停",failed:"传输失败",transferring:"传输中",canceled:"已取消"})[state] || state }
    ColumnLayout { anchors.fill: parent; anchors.margins: 32; spacing: 25
        RowLayout { Layout.fillWidth: true; ColumnLayout { spacing: 8; Text { text: "文件中心"; color: "#29364e"; font.pixelSize: 25; font.weight: Font.DemiBold } Text { text: "管理你的传输任务，随时暂停与继续"; color: "#909cae"; font.pixelSize: 12 } } Item { Layout.fillWidth: true } UiButton { text: "添加文件"; iconName: "plus"; primary: true; onClicked: demo.chooseFile() } }
        Rectangle { Layout.fillWidth: true; height: 60; radius: 12; color: "#edf2ff"
            RowLayout { anchors.fill: parent; anchors.margins: 16; spacing: 12; Image { source: "qrc:/icons/info.svg"; width: 18; height: 18 } ColumnLayout { spacing: 5; Text { text: "传输状态设计预览"; color: "#6a7fa9"; font.pixelSize: 12; font.weight: Font.DemiBold } Text { text: "进度为本地模拟。原型不会读取、上传或下载本机文件。真实传输接入前先验证中断、重试与取消。"; color: "#8b9bb7"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true } } }
        }
        RowLayout { spacing: 8; Repeater { model: [{key:"all",label:"全部文件"},{key:"active",label:"进行中"},{key:"completed",label:"已完成"}]; delegate: UiButton { required property var modelData; text: modelData.label; primary: files.filter===modelData.key; onClicked: files.filter=modelData.key } } Item { Layout.fillWidth: true } Text { text: demo.transfers.count+" 个任务"; color: "#a3adbc"; font.pixelSize: 11 } }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 14; color: "white"; border.color: "#e5e9f1"
            ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 16
                RowLayout { Layout.fillWidth: true; spacing: 18; Text { text: "文件 / 会话"; color: "#a4aebe"; font.pixelSize: 11; Layout.fillWidth: true } Text { text: "大小"; color: "#a4aebe"; font.pixelSize: 11; Layout.preferredWidth: 100 } Text { text: "传输状态"; color: "#a4aebe"; font.pixelSize: 11; Layout.preferredWidth: 210 } Text { text: "操作"; color: "#a4aebe"; font.pixelSize: 11; Layout.preferredWidth: 170 } }
                Rectangle { height: 1; Layout.fillWidth: true; color: "#edf0f5" }
                ListView { id: transfers; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: demo.transfers; spacing: 10; ScrollBar.vertical: ScrollBar {}
                    delegate: Item { id: task; required property var item; required property int index; property bool match: files.filter === "all" || files.filter === "completed" && item.state === "completed" || files.filter === "active" && (item.state === "paused" || item.state === "transferring" || item.state === "failed"); width: transfers.width; height: match?106:0; visible: match
                        RowLayout { anchors.fill: parent; spacing: 18
                            Rectangle { width: 46; height: 52; radius: 9; color: task.item.name.endsWith(".pdf") ? "#fff0eb" : "#eef1ff"; Text { anchors.centerIn: parent; text: task.item.name.split(".").pop().toUpperCase(); color: task.item.name.endsWith(".pdf") ? "#ca9683" : "#99a4ce"; font.pixelSize: 10; font.weight: Font.Bold } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 10; Text { text: task.item.name; font.pixelSize: 13; color: "#516078"; Layout.fillWidth: true; elide: Text.ElideRight } Text { text: task.item.direction+" · "+task.item.target; color: "#a0aabb"; font.pixelSize: 10 } }
                            Text { text: task.item.size; font.pixelSize: 11; color: "#9ba5b6"; Layout.preferredWidth: 100 }
                            ColumnLayout { Layout.preferredWidth: 210; Layout.minimumWidth: 210; Layout.maximumWidth: 210; spacing: 10
                                RowLayout { Layout.fillWidth: true; Text { text: files.stateLabel(task.item.state); color: task.item.state === "failed" ? "#b9857c" : "#8594b0"; font.pixelSize: 11 } Item { Layout.fillWidth: true } Text { text: Math.round(task.item.progress*100)+"%"; color: "#a3adbd"; font.pixelSize: 10 } }
                                ProgressBar { value: task.item.progress; Layout.fillWidth: true; implicitHeight: 4; background: Rectangle { radius: 2; color: "#edf0f6" } contentItem: Item { Rectangle { width: parent.width * task.item.progress; height: 4; radius: 2; color: task.item.state === "completed" ? "#97b3a4" : "#9aaee9" } } }
                            }
                            RowLayout { Layout.preferredWidth: 170; Layout.minimumWidth: 170; Layout.maximumWidth: 170; spacing: 3
                                UiButton { visible: task.item.state === "paused" || task.item.state === "failed"; text: task.item.state === "failed" ? "重试" : "继续"; quiet: true; onClicked: demo.transferAction(task.index,"resume") }
                                UiButton { visible: task.item.state === "transferring"; text: "暂停"; quiet: true; onClicked: demo.transferAction(task.index,"pause") }
                                UiButton { visible: task.item.state !== "completed" && task.item.state !== "canceled"; text: "取消"; quiet: true; onClicked: shell.confirmAction("取消传输","取消 “"+task.item.name+"” 的任务？已完成的文件与其他任务不受影响。","cancel-transfer",task.index) }
                                Badge { visible: task.item.state === "completed"; label: "已校验 · 示例"; fill: "#f0f6f3"; tint: "#93aa9e" }
                                Badge { visible: task.item.state === "canceled"; label: "保留任务记录"; fill: "#f1f3f7"; tint: "#a0a9b8" }
                            }
                        }
                        Rectangle { height: 1; width: parent.width; color: "#f0f2f7"; anchors.bottom: parent.bottom }
                    }
                    Text { visible: transfers.contentHeight===0; anchors.centerIn: parent; text: "这里暂时没有任务"; color: "#a4afc0"; font.pixelSize: 13 }
                }
            }
        }
    }
}
