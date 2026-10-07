import QtQuick
Rectangle {
    id: avatar
    property string label: ""
    property color tint: "#6474df"
    property bool online: false
    property bool showStatus: false
    implicitWidth: 42; implicitHeight: 42; radius: 12; color: tint
    Text { anchors.centerIn: parent; text: avatar.label; color: "white"; font.pixelSize: avatar.width > 50 ? 22 : 16; font.weight: Font.DemiBold }
    Rectangle { visible: avatar.showStatus; width: 11; height: 11; radius: 6; color: avatar.online ? "#64aa88" : "#b1b8c3"; border.color: "white"; border.width: 2; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: -1 }
}
