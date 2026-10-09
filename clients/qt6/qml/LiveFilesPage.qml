import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    required property var shell
    ColumnLayout { anchors.fill: parent; anchors.margins: 28; spacing: 20
        RowLayout { Layout.fillWidth: true; ColumnLayout { Text { text: "文件传输"; color: "#29364e"; font.pixelSize: 25 } Text { text: "分块续传 · SHA-256 校验 · 账号隔离"; color: "#919bae"; font.pixelSize: 12 } } Item { Layout.fillWidth: true } Badge { label: liveSession.fileReady?"文件服务已连接":"文件服务未连接" } UiButton { text: "选择文件"; primary: true; enabled: liveSession.fileReady; onClicked: liveSession.chooseFile() } }
        Text { text: "从会话上传的文件会在校验完成后发送分享卡片。下载文件保存至“下载/TinyIMX”；取消后保留任务与未完成文件用于复盘。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#8c98ad"; font.pixelSize: 12 }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "white"; radius: 14
            ListView { id: list; anchors.fill: parent; anchors.margins: 20; clip: true; spacing: 14; model: liveSession.transfers; ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle { required property var item; required property int index; width: list.width; height: 118; color: "#f8f9fc"; radius: 10
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 9
                        RowLayout { Layout.fillWidth: true; Text { text: item.name; color: "#42516c"; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight } Text { text: item.direction==="upload"?"上传":"下载"; color: "#939db0" } Text { text: item.size || "正在读取元信息"; color: "#939db0" } Badge { label: item.state==="completed"?"已完成并校验":item.state==="transferring"?"传输中":item.state==="paused"?"已暂停":item.state==="canceling"?"等待取消确认":item.state==="canceled"?"已取消":"失败" } }
                        ProgressBar { value: item.progress; Layout.fillWidth: true }
                        RowLayout { Layout.fillWidth: true; Text { text: item.error || (item.direction==="upload"?"接收范围："+item.target:"文件 ID："+item.file_id); color: item.error?"#be8379":"#98a2b4"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                            UiButton { visible: item.state==="transferring"; text: "暂停"; quiet: true; onClicked: liveSession.transferAction(index,"pause") }
                            UiButton { visible: item.state==="paused"||item.state==="failed"; text: item.state==="failed"?"重试":"继续"; quiet: true; enabled: liveSession.fileReady; onClicked: liveSession.transferAction(index,"resume") }
                            UiButton { visible: item.state==="transferring"||item.state==="paused"||item.state==="failed"; text: "取消"; quiet: true; onClicked: shell.confirmAction("取消传输","取消本任务；现有文件和其他任务会保留。","cancel-transfer",index) }
                            UiButton { visible: item.state==="completed"; text: "打开所在目录"; quiet: true; onClicked: liveSession.transferAction(index,"open") }
                        }
                    }
                }
                Text { visible: list.count===0; anchors.centerIn: parent; text: "暂无传输任务。选择文件上传，或点击会话中的文件卡片下载。"; color: "#96a1b4" }
            }
        }
    }
}
