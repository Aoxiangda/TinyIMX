import QtQuick
import QtQuick.Controls
Button {
    id: button
    property string iconName: ""
    property bool primary: false
    property bool quiet: false
    property bool danger: false
    property string hint: text
    implicitHeight: 36
    implicitWidth: text.length ? content.implicitWidth + 24 : 36
    hoverEnabled: true
    padding: 12
    Accessible.name: hint
    background: Rectangle {
        radius: 8
        color: !button.enabled ? "#eef0f4" : button.primary ? (button.down ? "#3451c5" : button.hovered ? "#4364df" : "#4c6aeb") : button.hovered ? "#eef1f7" : button.quiet ? "transparent" : "#ffffff"
        border.width: button.primary || button.quiet ? 0 : 1
        border.color: button.activeFocus ? "#4c6aeb" : "#dfe4ed"
    }
    contentItem: Item {
        id: content
        implicitWidth: inner.implicitWidth
        implicitHeight: 20
        Row {
        id: inner
        anchors.centerIn: parent; spacing: 7
        Image { visible: button.iconName !== ""; source: visible ? "qrc:/icons/" + button.iconName + ".svg" : ""; width: visible ? 18 : 0; height: 18; anchors.verticalCenter: parent.verticalCenter; opacity: button.enabled ? 1 : 0.35 }
        Text { text: button.text; color: !button.enabled ? "#a5afbf" : button.primary ? "white" : button.danger ? "#c55050" : "#4d596d"; font.pixelSize: 13; font.weight: button.primary ? Font.DemiBold : Font.Normal; anchors.verticalCenter: parent.verticalCenter }
        }
    }
    ToolTip.visible: hovered && hint !== ""
    ToolTip.text: hint
    ToolTip.delay: 650
}
