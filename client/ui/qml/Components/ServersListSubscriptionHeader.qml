import QtQuick
import QtQuick.Layouts

import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

Item {
    id: root

    required property string section

    implicitHeight: root.section === "" ? 0 : header.implicitHeight + 24
    visible: root.section !== ""

    function isBlockedByConnection(message) {
        if (ConnectionController.isConnected) {
            PageController.showNotificationMessage(message)
            return true
        }
        return false
    }

    function removeSubscription() {
        if (root.isBlockedByConnection(qsTr("Unable to remove the subscription while there is an active connection"))) {
            return
        }

        const subscriptionId = root.section
        const headerText = qsTr("Remove subscription \"%1\"?").arg(ImportController.subscriptionName(subscriptionId))
        const descriptionText = qsTr("The subscription and all its servers will be removed from the application.")

        Dialogs.askQuestion(headerText, descriptionText, qsTr("Remove"), qsTr("Cancel"), function() {
            if (!root.isBlockedByConnection(qsTr("Unable to remove the subscription while there is an active connection"))) {
                ImportController.removeSubscription(subscriptionId)
            }
        }, function() {})
    }

    RowLayout {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 16

        spacing: 8

        H3TextType {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter

            text: ImportController.subscriptionName(root.section)
        }

        IconButtonType {
            Layout.alignment: Qt.AlignVCenter

            size: IconButtonType.Small
            icon.source: "qrc:/images/controls/refresh-cw.svg"
            Accessible.name: qsTr("Update subscription")

            onClicked: {
                if (root.isBlockedByConnection(qsTr("Unable to update the subscription while there is an active connection"))) {
                    return
                }

                PageController.showBusyIndicator(true)
                // Updating rebuilds the servers list and destroys this header, so it must not run inside its own click handler
                Qt.callLater(ImportController.updateSubscription, root.section)
                Qt.callLater(PageController.showBusyIndicator, false)
            }
        }

        IconButtonType {
            Layout.alignment: Qt.AlignVCenter

            size: IconButtonType.Small
            icon.source: "qrc:/images/controls/trash-2.svg"
            Accessible.name: qsTr("Remove subscription")

            onClicked: root.removeSubscription()
        }
    }
}
