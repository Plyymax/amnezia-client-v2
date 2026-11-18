import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Style 1.0

import "TextTypes"

ToolTip {
    id: root

    property string title

    readonly property int maximumWidth: 280

    width: Math.min(root.maximumWidth, Math.max(titleText.implicitWidth, bodyText.implicitWidth) + leftPadding + rightPadding)

    x: parent ? Math.round((parent.width - root.width) / 2) : 0
    y: -root.height - 8

    padding: 12
    margins: 16

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 }
    }

    background: Item {
        Rectangle {
            id: panelShadowSource

            anchors.fill: parent

            radius: 12
            color: AmneziaStyle.color.bgSurface
            visible: false
        }

        ShadowType {
            anchors.fill: panelShadowSource
            source: panelShadowSource
            visible: root.visible
            verticalOffset: 8
            blur: 24
            color: AmneziaStyle.color.bgOverlay
        }

        Rectangle {
            anchors.fill: parent

            radius: 12
            color: AmneziaStyle.color.bgSurface
            border.width: 1
            border.color: AmneziaStyle.color.borderSoft
        }
    }

    contentItem: ColumnLayout {
        spacing: 4

        SmallTextType {
            id: titleText

            Layout.fillWidth: true

            visible: root.title !== ""
            text: root.title
            color: AmneziaStyle.color.textPrimary
            wrapMode: Text.WordWrap
        }

        CaptionTextType {
            id: bodyText

            Layout.fillWidth: true

            text: root.text
            color: AmneziaStyle.color.textSecondary
        }
    }
}
