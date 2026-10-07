import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    required property var shell
    ColumnLayout { anchors.fill: parent; anchors.margins: 30; spacing: 20
        RowLayout { Layout.fillWidth: true; Text { text: "AI 助手"; color: "#29364e"; font.pixelSize: 25 } Item { Layout.fillWidth: true } Badge { label: liveSession.aiModel+" · "+liveSession.aiState } }
        Rectangle { Layout.fillWidth: true; implicitHeight: 120; radius: 12; color: "white"
            ColumnLayout { anchors.fill: parent; anchors.margins: 18; spacing: 12
                RowLayout { Layout.fillWidth: true; Text { text: "Ollama 服务"; color: "#6c7990" } TextField { id: address; Layout.fillWidth: true; text: liveSession.aiEndpoint; selectByMouse: true } TextField { id: model; Layout.preferredWidth: 180; text: liveSession.aiModel; selectByMouse: true } UiButton { text: "应用并检测"; onClicked: { liveSession.configureAI(address.text,model.text);liveSession.refreshAIModels() } } }
                Text { text: liveSession.aiModels.length?"已安装模型："+liveSession.aiModels.join("、"):"输入会发往所填的模型服务。默认连接本机 Ollama；模型失败时其他功能可继续使用。"; color: "#929db0"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 14; color: "white"
            ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 16
                RowLayout { visible: liveSession.aiState==="thinking"; Layout.fillWidth: true; BusyIndicator { running: visible; implicitWidth: 30; implicitHeight: 30 } Text { text: "模型正在生成…"; color: "#9483b5" } Item { Layout.fillWidth: true } UiButton { text: "停止生成"; onClicked: liveSession.cancelAI() } }
                ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; TextArea { readOnly: true; selectByMouse: true; text: liveSession.aiAnswer.length?liveSession.aiAnswer:"输入问题，或选择附加当前会话片段。"; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; color: liveSession.aiState==="error"?"#b97870":"#53627c"; background: Item {} } }
                CheckBox { id: context; text: "附加当前已加载会话的最近 20 条消息（只读）"; checked: false }
                TextArea { id: prompt; Layout.fillWidth: true; Layout.preferredHeight: 110; placeholderText: "请输入问题…"; selectByMouse: true; wrapMode: TextEdit.Wrap; background: Rectangle { color: "#f7f8fc"; radius: 10 } }
                RowLayout { Layout.fillWidth: true; Text { text: "助手不能修改好友、群组或文件。切换账号会清除本轮内容。"; color: "#9ba5b5"; font.pixelSize: 11 } Item { Layout.fillWidth: true } UiButton { text: liveSession.aiState==="error"?"重试":"发送"; primary: true; enabled: liveSession.authenticated&&liveSession.aiState!=="thinking"&&prompt.text.trim().length>0; onClicked: liveSession.askAI(prompt.text,context.checked) } }
            }
        }
    }
}
