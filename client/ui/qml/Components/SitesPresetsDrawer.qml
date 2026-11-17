import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../Controls2"

SheetType {
    id: root

    expandedHeight: Math.min(presetsGrid.implicitHeight + 24 + 24 + PageController.safeAreaBottomMargin, windowHeight * 0.9)

    FlickableType {
        anchors.top: parent.top

        contentHeight: presetsGrid.implicitHeight + 24 + 24 + PageController.safeAreaBottomMargin

        SitesPresetsGrid {
            id: presetsGrid

            anchors.top: parent.top
            anchors.topMargin: 24
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
        }
    }
}
