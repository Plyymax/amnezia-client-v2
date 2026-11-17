import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"

AbstractButton {
    id: root

    property string description
    property string iconSource
    property bool added: false
    property bool loading: false

    implicitHeight: content.implicitHeight + topPadding + bottomPadding

    padding: 12

    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.4

    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: root.added
    Accessible.name: root.text
    Accessible.description: root.description

    background: Rectangle {
        radius: 16

        color: root.added ? AmneziaStyle.color.bgSelected : AmneziaStyle.color.bgSurface
        border.width: root.added || (root.hovered && !root.down) ? 1 : 0
        border.color: root.added ? AmneziaStyle.color.accentLight : AmneziaStyle.color.borderSoft

        ListItemStateLayerType {
            anchors.fill: parent

            radius: parent.radius
            hovered: root.hovered && !root.added
            pressed: root.down
        }

        FocusRingType {
            radius: 16
            visible: root.visualFocus
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
            enabled: root.enabled
        }
    }

    contentItem: Item {
        implicitHeight: content.implicitHeight

        ColumnLayout {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right

            spacing: 8

            IconImage {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24

                source: root.iconSource
                sourceSize: Qt.size(24, 24)
                color: root.added ? AmneziaStyle.color.accentLight : AmneziaStyle.color.iconSecondary
            }

            ColumnLayout {
                Layout.fillWidth: true

                spacing: 2

                SmallTextType {
                    Layout.fillWidth: true

                    text: root.text
                    color: AmneziaStyle.color.textPrimary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                CaptionTextType {
                    Layout.fillWidth: true

                    text: root.description
                    color: AmneziaStyle.color.textQuaternary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right

            width: 20
            height: 20
            radius: 10

            visible: root.added && !root.loading
            color: AmneziaStyle.color.accentLight

            IconImage {
                anchors.centerIn: parent

                width: 14
                height: 14

                source: "qrc:/images/controls/check.svg"
                sourceSize: Qt.size(14, 14)
                color: AmneziaStyle.color.iconInverted
            }
        }

        SpinnerType {
            anchors.top: parent.top
            anchors.right: parent.right

            size: SpinnerType.Small
            running: root.loading
            visible: root.loading
            color: AmneziaStyle.color.iconSecondary
        }
    }

    Keys.onEnterPressed: root.clicked()
    Keys.onReturnPressed: root.clicked()
}
