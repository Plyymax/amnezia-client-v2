#include "ipSplitTunnelingController.h"
#include "core/utils/networkUtilities.h"
#include <QDebug>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace
{
    constexpr int presetDownloadTimeoutMs = 30000;

    const QString opencckGroupUrl = QStringLiteral("https://iplist.opencck.org/?format=amnezia&data=cidr4&group=%1");
    const QString lib4uReleaseUrl =
            QStringLiteral("https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/%1");

    QVector<SitesPreset> allSitesPresets()
    {
        QVector<SitesPreset> presets;
        const QStringList opencckGroups = { "youtube", "messengers", "socials",   "ai",      "discord",
                                            "video",   "music",      "games",     "news",    "anime",
                                            "art",     "education",  "tools",     "torrent", "shop",
                                            "finance", "hosting",    "jetbrains", "casino" };
        for (const QString &group : opencckGroups) {
            presets.append({ group, RouteMode::VpnOnlyForwardSites, QUrl(opencckGroupUrl.arg(group)) });
        }

        presets.append({ "ru-sites", RouteMode::VpnAllExceptSites, QUrl(lib4uReleaseUrl.arg("amnezia.json")), true });
        presets.append({ "ru-services", RouteMode::VpnAllExceptSites, QUrl(lib4uReleaseUrl.arg("amnezia-ip-lite.json")) });
        presets.append(
                { "ru-networks", RouteMode::VpnAllExceptSites, QUrl(lib4uReleaseUrl.arg("amnezia-ip.json")), true });
        return presets;
    }

    bool isAvailableOnPlatform(const SitesPreset &preset)
    {
#ifdef AMNEZIA_DESKTOP
        Q_UNUSED(preset)
        return true;
#else
        return !preset.desktopOnly;
#endif
    }

    QSet<QString> presetsHostnames(const QVariantMap &presets)
    {
        QSet<QString> hostnames;
        for (const QVariant &presetSites : presets) {
            const QStringList sites = presetSites.toStringList();
            hostnames.unite(QSet<QString>(sites.cbegin(), sites.cend()));
        }
        return hostnames;
    }
}

IpSplitTunnelingController::IpSplitTunnelingController(SecureAppSettingsRepository* appSettingsRepository, QObject* parent)
    : QObject(parent),
      m_appSettingsRepository(appSettingsRepository)
{
    m_currentRouteMode = m_appSettingsRepository->routeMode();
    if (m_currentRouteMode == RouteMode::VpnAllSites) { // for old split tunneling configs
        m_appSettingsRepository->setRouteMode(RouteMode::VpnOnlyForwardSites);
        m_currentRouteMode = RouteMode::VpnOnlyForwardSites;
    }
    fillSites();
}

bool IpSplitTunnelingController::addSiteInternal(const QString &hostname, const QStringList &ips)
{
    QVariantMap existing = m_appSettingsRepository->vpnSites(m_currentRouteMode);
    if (existing.contains(hostname) && ips.isEmpty()) {
        return false;
    }

    for (int i = 0; i < m_sites.size(); i++) {
        if (m_sites[i].first == hostname) {
            bool changed = false;
            for (const QString &ip : ips) {
                if (!ip.isEmpty() && !m_sites[i].second.contains(ip)) {
                    m_sites[i].second.append(ip);
                    changed = true;
                }
            }
            if (!changed) {
                return false;
            }
            m_appSettingsRepository->addVpnSite(m_currentRouteMode, hostname, ips);
            return true;
        }
    }
    m_sites.append(qMakePair(hostname, ips));
    m_appSettingsRepository->addVpnSite(m_currentRouteMode, hostname, ips);
    return true;
}

void IpSplitTunnelingController::addSites(const QMap<QString, QStringList> &sites, bool replaceExisting)
{
    if (replaceExisting) {
        m_sites.clear();
    }
    for (auto it = sites.constBegin(); it != sites.constEnd(); ++it) {
        const QString &hostname = it.key();
        const QStringList &ips = it.value();
        bool found = false;
        for (int i = 0; i < m_sites.size(); i++) {
            if (m_sites[i].first == hostname) {
                for (const QString &ip : ips) {
                    if (!ip.isEmpty() && !m_sites[i].second.contains(ip)) {
                        m_sites[i].second.append(ip);
                    }
                }
                found = true;
                break;
            }
        }
        if (!found) {
            m_sites.append(qMakePair(hostname, ips));
        }
    }
    if (replaceExisting) {
        m_appSettingsRepository->removeAllVpnSites(m_currentRouteMode);
    }
    m_appSettingsRepository->addVpnSites(m_currentRouteMode, sites);
}

bool IpSplitTunnelingController::addSite(const QString &hostname)
{
    QString normalizedHostname = normalizeHostname(hostname);
    
    if (!validateHostname(normalizedHostname)) {
        return false;
    }
    
    if (NetworkUtilities::ipAddressWithSubnetRegExp().exactMatch(normalizedHostname)) {
        processSite(normalizedHostname, {});
        return true;
    }
    
    if (addSiteInternal(normalizedHostname, {})) {
        QHostInfo::lookupHost(normalizedHostname, this, SLOT(onHostResolved(QHostInfo)));
        return true;
    }
    
    return false;
}

bool IpSplitTunnelingController::removeSite(const QString &hostname)
{
    for (int i = 0; i < m_sites.size(); i++) {
        if (m_sites[i].first == hostname) {
            m_sites.removeAt(i);
            m_appSettingsRepository->removeVpnSite(m_currentRouteMode, hostname);
            return true;
        }
    }
    return false;
}

void IpSplitTunnelingController::removeSites()
{
    m_sites.clear();
    m_appSettingsRepository->removeAllVpnSites(m_currentRouteMode);
}

void IpSplitTunnelingController::setRouteMode(RouteMode routeMode)
{
    m_currentRouteMode = routeMode;
    fillSites();
    m_appSettingsRepository->setRouteMode(routeMode);
}

void IpSplitTunnelingController::toggleSplitTunneling(bool enabled)
{
    m_appSettingsRepository->setSitesSplitTunnelingEnabled(enabled);
}

RouteMode IpSplitTunnelingController::getRouteMode() const
{
    return m_currentRouteMode;
}

bool IpSplitTunnelingController::isSplitTunnelingEnabled() const
{
    return m_appSettingsRepository->isSitesSplitTunnelingEnabled();
}

QVector<QPair<QString, QStringList>> IpSplitTunnelingController::getCurrentSites() const
{
    return m_sites;
}

void IpSplitTunnelingController::fillSites()
{
    QVariantMap sitesMap = m_appSettingsRepository->vpnSites(m_currentRouteMode);
    m_sites.clear();
    for (auto it = sitesMap.begin(); it != sitesMap.end(); ++it) {
        m_sites.append(qMakePair(it.key(), SecureAppSettingsRepository::siteIpList(it.value())));
    }
}

QString IpSplitTunnelingController::normalizeHostname(const QString &hostname) const
{
    QString normalized = hostname;
    normalized.replace("https://", "");
    normalized.replace("http://", "");
    normalized.replace("ftp://", "");

    if (NetworkUtilities::ipAddressWithSubnetRegExp().exactMatch(normalized)) {
        return normalized;
    }

    const QStringList parts = normalized.split("/", Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : parts.first();
}

bool IpSplitTunnelingController::validateHostname(const QString &hostname) const
{
    if (hostname.isEmpty()) {
        return false;
    }
    if (!hostname.contains(".") && !NetworkUtilities::ipAddressWithSubnetRegExp().exactMatch(hostname)) {
        return false;
    }
    return true;
}


void IpSplitTunnelingController::onHostResolved(const QHostInfo &hostInfo)
{
    const QList<QHostAddress> &addresses = hostInfo.addresses();
    QString hostname = hostInfo.hostName();

    QStringList allIpv4;
    for (const QHostAddress &addr : addresses) {
        if (addr.protocol() == QAbstractSocket::NetworkLayerProtocol::IPv4Protocol) {
            allIpv4.append(addr.toString());
        }
    }
    allIpv4.removeDuplicates();
    qDebug() << "[SplitTunneling] Host resolved:" << hostname
             << "-> adding all IPv4 addresses to list:" << allIpv4;

    if (!allIpv4.isEmpty()) {
        processSiteAfterResolve(hostname, allIpv4);
    }
}

void IpSplitTunnelingController::processSiteAfterResolve(const QString &hostname, const QStringList &ips)
{
    for (int i = 0; i < m_sites.size(); i++) {
        if (m_sites[i].first == hostname) {
            for (const QString &ip : ips) {
                if (!ip.isEmpty() && !m_sites[i].second.contains(ip)) {
                    m_sites[i].second.append(ip);
                }
            }
            break;
        }
    }
    m_appSettingsRepository->addVpnSite(m_currentRouteMode, hostname, ips);
}

void IpSplitTunnelingController::processSite(const QString &hostname, const QStringList &ips)
{
    addSiteInternal(hostname, ips);
}

bool IpSplitTunnelingController::importSitesFromJson(const QByteArray& jsonData, bool replaceExisting, QString &errorMessage)
{
    QMap<QString, QStringList> sites;
    if (!parseSitesJson(jsonData, sites, errorMessage)) {
        return false;
    }

    addSites(sites, replaceExisting);

    return true;
}

bool IpSplitTunnelingController::parseSitesJson(const QByteArray &jsonData, QMap<QString, QStringList> &sites,
                                                QString &errorMessage) const
{
    QJsonParseError parseError;
    QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        errorMessage = tr("Failed to parse JSON data: %1").arg(parseError.errorString());
        return false;
    }
    
    if (!jsonDocument.isArray()) {
        errorMessage = tr("The JSON data is not an array");
        return false;
    }
    
    QJsonArray jsonArray = jsonDocument.array();

    for (auto jsonValue : jsonArray) {
        QJsonObject jsonObject = jsonValue.toObject();
        QString hostname = jsonObject.value("hostname").toString("");

        QStringList ips;
        if (jsonObject.value("ips").isArray()) {
            const QJsonArray ipsArray = jsonObject.value("ips").toArray();
            for (const auto &ipValue : ipsArray) {
                ips.append(ipValue.toString());
            }
        }
        const QString singleIp = jsonObject.value("ip").toString("");
        if (!singleIp.isEmpty()) {
            ips.append(singleIp);
        }
        ips.removeAll(QString());
        ips.removeDuplicates();
        
        QString normalizedHostname = normalizeHostname(hostname);
        
        if (!validateHostname(normalizedHostname)) {
            qDebug() << normalizedHostname << " not look like ip adress or domain name";
            continue;
        }
        
        sites.insert(normalizedHostname, ips);
    }

    return true;
}

QByteArray IpSplitTunnelingController::exportSitesToJson() const
{
    QVector<QPair<QString, QStringList>> sites = getCurrentSites();
    QJsonArray jsonArray;
    
    for (const auto &site : sites) {
        QJsonObject jsonObject;
        jsonObject["hostname"] = site.first;

        QJsonArray ipsArray;
        for (const QString &ip : site.second) {
            ipsArray.append(ip);
        }
        jsonObject["ips"] = ipsArray;
        // Keep the legacy "ip" field (first address) for backward compatibility.
        jsonObject["ip"] = site.second.isEmpty() ? QString() : site.second.first();

        jsonArray.append(jsonObject);
    }
    
    QJsonDocument jsonDocument(jsonArray);
    return jsonDocument.toJson();
}

QVector<SitesPreset> IpSplitTunnelingController::sitesPresets(RouteMode routeMode)
{
    QVector<SitesPreset> presets;
    for (const SitesPreset &preset : allSitesPresets()) {
        if (preset.routeMode == routeMode && isAvailableOnPlatform(preset)) {
            presets.append(preset);
        }
    }
    return presets;
}

QStringList IpSplitTunnelingController::addedPresetIds() const
{
    return m_appSettingsRepository->vpnSitesPresets(m_currentRouteMode).keys();
}

bool IpSplitTunnelingController::isPresetLoading(const QString &presetId) const
{
    return m_loadingPresets.contains(presetId);
}

bool IpSplitTunnelingController::addPreset(const QString &presetId)
{
    const QVector<SitesPreset> presets = sitesPresets(m_currentRouteMode);
    const auto preset = std::find_if(presets.cbegin(), presets.cend(),
                                     [&presetId](const SitesPreset &item) { return item.id == presetId; });
    if (preset == presets.cend() || m_loadingPresets.contains(presetId) || addedPresetIds().contains(presetId)) {
        return false;
    }

    QNetworkRequest request(preset->url);
    request.setTransferTimeout(presetDownloadTimeoutMs);

    m_loadingPresets.insert(presetId);
    QNetworkReply *reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, preset = *preset]() {
        reply->deleteLater();
        m_loadingPresets.remove(preset.id);

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[SplitTunneling] Failed to download sites preset" << preset.id << reply->errorString();
            emit presetAddFailed(preset.id);
            return;
        }

        QMap<QString, QStringList> sites;
        QString errorMessage;
        if (!parseSitesJson(reply->readAll(), sites, errorMessage) || sites.isEmpty()) {
            qWarning() << "[SplitTunneling] Failed to parse sites preset" << preset.id << errorMessage;
            emit presetAddFailed(preset.id);
            return;
        }

        applyPreset(preset, sites);
    });
    return true;
}

void IpSplitTunnelingController::applyPreset(const SitesPreset &preset, const QMap<QString, QStringList> &sites)
{
    QVariantMap presets = m_appSettingsRepository->vpnSitesPresets(preset.routeMode);
    const QSet<QString> sitesFromPresets = presetsHostnames(presets);
    const QVariantMap existingSites = m_appSettingsRepository->vpnSites(preset.routeMode);

    QMap<QString, QStringList> newSites;
    QStringList presetSites;
    for (auto it = sites.constBegin(); it != sites.constEnd(); ++it) {
        if (!existingSites.contains(it.key())) {
            newSites.insert(it.key(), it.value());
            presetSites.append(it.key());
        } else if (sitesFromPresets.contains(it.key())) {
            presetSites.append(it.key());
        }
    }

    presets.insert(preset.id, presetSites);
    m_appSettingsRepository->setVpnSitesPresets(preset.routeMode, presets);

    if (preset.routeMode == m_currentRouteMode) {
        for (auto it = newSites.constBegin(); it != newSites.constEnd(); ++it) {
            m_sites.append(qMakePair(it.key(), it.value()));
        }
    }
    m_appSettingsRepository->addVpnSites(preset.routeMode, newSites);

    emit presetAdded(preset.id);
}

bool IpSplitTunnelingController::removePreset(const QString &presetId)
{
    QVariantMap presets = m_appSettingsRepository->vpnSitesPresets(m_currentRouteMode);
    if (!presets.contains(presetId)) {
        return false;
    }

    const QStringList presetSites = presets.take(presetId).toStringList();
    const QSet<QString> sitesFromOtherPresets = presetsHostnames(presets);

    QStringList sitesToRemove;
    for (const QString &hostname : presetSites) {
        if (!sitesFromOtherPresets.contains(hostname)) {
            sitesToRemove.append(hostname);
        }
    }

    const QSet<QString> sitesToRemoveSet(sitesToRemove.cbegin(), sitesToRemove.cend());
    m_sites.removeIf([&sitesToRemoveSet](const QPair<QString, QStringList> &site) {
        return sitesToRemoveSet.contains(site.first);
    });

    m_appSettingsRepository->setVpnSitesPresets(m_currentRouteMode, presets);
    m_appSettingsRepository->removeVpnSites(m_currentRouteMode, sitesToRemove);
    return true;
}
