import QtQuick
Rectangle {
    id: badge
    property string label: ""
    property color tint: "#6474df"
    property color fill: "#edf0ff"
    implicitHeight: 24; implicitWidth: tag.implicitWidth + 16; radius: 6; color: fill
    Text { id: tag; anchors.centerIn: parent; text: badge.label; color: badge.tint; font.pixelSize: 11 }
}
