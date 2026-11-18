import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

ColumnLayout {
    id: root

    property int maxVisibleCount: -1

    signal showAllClicked()

    spacing: 12

    RowLayout {
        Layout.fillWidth: true

        spacing: 8

        H3TextType {
            Layout.fillWidth: true

            text: qsTr("Ready-made sets")
        }

        LinkButtonType {
            visible: root.maxVisibleCount >= 0 && presetsRepeater.count > root.maxVisibleCount
            text: qsTr("All %1").arg(presetsRepeater.count)

            onClicked: root.showAllClicked()
        }
    }

    GridLayout {
        Layout.fillWidth: true

        columns: 3
        columnSpacing: 8
        rowSpacing: 8

        Repeater {
            id: presetsRepeater

            model: IpSplitTunnelingController.presetsModel

            SitesPresetTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1

                visible: root.maxVisibleCount < 0 || index < root.maxVisibleCount

                text: name
                description: model.description
                services: model.services
                iconSource: iconPath
                added: isAdded
                loading: isLoading

                onClicked: {
                    if (isLoading) {
                        return
                    }
                    if (!isAdded) {
                        IpSplitTunnelingController.addPreset(presetId)
                        return
                    }

                    var removedPresetId = presetId
                    Dialogs.askQuestion(qsTr("Remove the %1 set?").arg(name),
                                        qsTr("Addresses from this set will be removed from the list, except those you added yourself or that belong to another added set."),
                                        qsTr("Remove"),
                                        qsTr("Cancel"),
                                        function() { IpSplitTunnelingController.removePreset(removedPresetId) },
                                        function() {})
                }
            }
        }
    }
}
