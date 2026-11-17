#include "ipSplitTunnelingUiController.h"

#include <QDebug>

#include "systemController.h"
#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"

IpSplitTunnelingUiController::IpSplitTunnelingUiController(IpSplitTunnelingController *ipSplitTunnelingController,
                                                           IpSplitTunnelingModel *ipSplitTunnelingModel, QObject *parent)
    : QObject(parent),
      m_ipSplitTunnelingController(ipSplitTunnelingController),
      m_ipSplitTunnelingModel(ipSplitTunnelingModel),
      m_presetsModel(new IpSplitTunnelingPresetsModel(this))
{
    m_ipSplitTunnelingModel->updateModel(m_ipSplitTunnelingController->getCurrentSites());
    updatePresetsModel();

    connect(m_ipSplitTunnelingController, &IpSplitTunnelingController::presetAdded, this, [this](const QString &presetId) {
        updatePresetsModel();
        emit finished(tr("Set added: %1").arg(IpSplitTunnelingPresetsModel::presetName(presetId)));
    });
    connect(m_ipSplitTunnelingController, &IpSplitTunnelingController::presetAddFailed, this,
            [this](const QString &presetId) {
                updatePresetsModel();
                emit errorOccurred(tr("Couldn't download the %1 set. Check your internet connection and try again.")
                                           .arg(IpSplitTunnelingPresetsModel::presetName(presetId)));
            });
}

void IpSplitTunnelingUiController::addSite(QString hostname)
{
    if (m_ipSplitTunnelingController->addSite(hostname)) {
        emit finished(tr("New site added: %1").arg(hostname));
    }
}

void IpSplitTunnelingUiController::removeSite(int index)
{
    auto modelIndex = m_ipSplitTunnelingModel->index(index);
    auto hostname = m_ipSplitTunnelingModel->data(modelIndex, IpSplitTunnelingModel::Roles::UrlRole).toString();
    if (m_ipSplitTunnelingController->removeSite(hostname)) {
        emit finished(tr("Site removed: %1").arg(hostname));
    }
}

void IpSplitTunnelingUiController::removeSites()
{
    m_ipSplitTunnelingController->removeSites();
    emit finished(tr("Site list cleared!"));
}

void IpSplitTunnelingUiController::importSites(const QString &fileName, bool replaceExisting)
{
    QByteArray jsonData;
    if (!SystemController::readFile(fileName, jsonData)) {
        emit errorOccurred(tr("Can't open file: %1").arg(fileName));
        return;
    }

    QString errorMessage;
    if (m_ipSplitTunnelingController->importSitesFromJson(jsonData, replaceExisting, errorMessage)) {
        emit finished(tr("Import completed"));
    } else {
        emit errorOccurred(errorMessage);
    }
}

void IpSplitTunnelingUiController::exportSites(const QString &fileName)
{
    QByteArray jsonData = m_ipSplitTunnelingController->exportSitesToJson();
    if (!SystemController::saveFile(fileName, jsonData)) {
        qInfo() << "IpSplitTunnelingUiController::exportSites: save or share was cancelled or failed";
        return;
    }
    emit finished(tr("Export completed"));
}

void IpSplitTunnelingUiController::toggleSplitTunneling(bool enabled)
{
    m_ipSplitTunnelingController->toggleSplitTunneling(enabled);
    emit isSplitTunnelingEnabledChanged();
}

void IpSplitTunnelingUiController::setRouteMode(int routeMode)
{
    m_ipSplitTunnelingController->setRouteMode(static_cast<amnezia::RouteMode>(routeMode));
    emit routeModeChanged();
}

int IpSplitTunnelingUiController::getRouteMode() const
{
    return static_cast<int>(m_ipSplitTunnelingController->getRouteMode());
}

bool IpSplitTunnelingUiController::isSplitTunnelingEnabled() const
{
    return m_ipSplitTunnelingController->isSplitTunnelingEnabled();
}

void IpSplitTunnelingUiController::addPreset(const QString &presetId)
{
    if (m_ipSplitTunnelingController->addPreset(presetId)) {
        updatePresetsModel();
    }
}

void IpSplitTunnelingUiController::removePreset(const QString &presetId)
{
    if (m_ipSplitTunnelingController->removePreset(presetId)) {
        emit finished(tr("Set removed: %1").arg(IpSplitTunnelingPresetsModel::presetName(presetId)));
    }
}

IpSplitTunnelingPresetsModel *IpSplitTunnelingUiController::presetsModel() const
{
    return m_presetsModel;
}

void IpSplitTunnelingUiController::updateModel()
{
    m_ipSplitTunnelingModel->updateModel(m_ipSplitTunnelingController->getCurrentSites());
    updatePresetsModel();
}

void IpSplitTunnelingUiController::updatePresetsModel()
{
    QStringList presetIds;
    QStringList loadingPresetIds;
    for (const SitesPreset &preset :
         IpSplitTunnelingController::sitesPresets(m_ipSplitTunnelingController->getRouteMode())) {
        presetIds.append(preset.id);
        if (m_ipSplitTunnelingController->isPresetLoading(preset.id)) {
            loadingPresetIds.append(preset.id);
        }
    }
    m_presetsModel->updateModel(presetIds, m_ipSplitTunnelingController->addedPresetIds(), loadingPresetIds);
}
