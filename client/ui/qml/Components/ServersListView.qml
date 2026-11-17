import QtQuick
import QtQuick.Controls

import PageEnum 1.0
import Style 1.0

import "../Controls2"

ListViewType {
    id: root

    readonly property real wheelScrollStep: 160

    signal serverSettingsOpened()

    leftMargin: 16
    rightMargin: 16
    bottomMargin: 16

    model: ServersModel

    section.property: "subscriptionId"
    section.criteria: ViewSection.FullString
    section.delegate: ServersListSubscriptionHeader {
        width: root.width - root.leftMargin - root.rightMargin
    }

    NumberAnimation {
        id: wheelScrollAnimation

        target: root
        property: "contentY"
        duration: 200
        easing.type: Easing.OutCubic
    }

    WheelHandler {
        enabled: root.interactive
        acceptedDevices: PointerDevice.Mouse

        onWheel: function(event) {
            const startY = wheelScrollAnimation.running ? wheelScrollAnimation.to : root.contentY
            const minY = root.originY - root.topMargin
            const maxY = Math.max(minY, root.originY + root.contentHeight + root.bottomMargin - root.height)
            wheelScrollAnimation.to = Math.max(minY, Math.min(maxY, startY - event.angleDelta.y / 120 * root.wheelScrollStep))
            wheelScrollAnimation.restart()
        }
    }

    function openServerSettings(serverId) {
        ServersUiController.setProcessedServerId(serverId)

        if (ServersUiController.isServerFromApi(ServersUiController.processedServerId)) {
            if (ServersUiController.isServerCountrySelectionAvailable(ServersUiController.processedServerId)) {
                root.serverSettingsOpened()
                PageController.goToPage(PageEnum.PageSettingsApiAvailableCountries)
            } else {
                PageController.showBusyIndicator(true)
                let result = SubscriptionUiController.getAccountInfo(ServersUiController.processedServerId, false)
                PageController.showBusyIndicator(false)
                if (!result) {
                    return
                }

                root.serverSettingsOpened()
                PageController.goToPage(PageEnum.PageSettingsApiServerInfo)
            }
        } else {
            root.serverSettingsOpened()
            PageController.goToPage(PageEnum.PageSettingsServerInfo)
        }
    }

    delegate: ServerListItem {
        readonly property bool subscriptionAlert: isServerFromGatewayApi && (isSubscriptionExpired || isSubscriptionExpiringSoon)

        width: root.width - root.leftMargin - root.rightMargin

        text: nameWithoutFlag
        flagCode: model.flagCode
        subtitle: subscriptionAlert
                  ? (isSubscriptionExpired ? qsTr("Subscription expired. Please renew") : qsTr("Subscription expiring soon"))
                  : (protocolNames !== "" ? serverDescription + " (" + protocolNames + ")" : serverDescription)
        subtitleColor: subscriptionAlert
                       ? (isSubscriptionExpired ? AmneziaStyle.color.statusDestructive : AmneziaStyle.color.statusWarning)
                       : AmneziaStyle.color.textTertiary
        showWarning: ServersUiController.serverHasOutdatedAwgContainer(serverId)
        showSeparator: index < root.count - 1

        checked: serverId === ServersUiController.defaultServerId

        onClicked: {
            if (ConnectionController.isConnected) {
                PageController.showNotificationMessage(qsTr("Unable change server while there is an active connection"))
                return
            }

            ServersUiController.setDefaultServerAtIndex(index)
        }

        onSettingsRequested: root.openServerSettings(serverId)
    }
}
