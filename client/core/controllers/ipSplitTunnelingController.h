#ifndef IPSPLITTUNNELINGCONTROLLER_H
#define IPSPLITTUNNELINGCONTROLLER_H

#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"
#include "core/repositories/secureAppSettingsRepository.h"

using namespace amnezia;

struct SitesPreset
{
    QString id;
    RouteMode routeMode;
    QUrl url;
    bool desktopOnly = false;
};

class IpSplitTunnelingController : public QObject
{
    Q_OBJECT

public:
    explicit IpSplitTunnelingController(SecureAppSettingsRepository* appSettingsRepository, QObject* parent = nullptr);

    bool addSite(const QString &hostname);
    void addSites(const QMap<QString, QStringList> &sites, bool replaceExisting);
    bool removeSite(const QString &hostname);
    void removeSites();
    void setRouteMode(RouteMode routeMode);
    void toggleSplitTunneling(bool enabled);

    RouteMode getRouteMode() const;
    bool isSplitTunnelingEnabled() const;
    QVector<QPair<QString, QStringList>> getCurrentSites() const;

    bool importSitesFromJson(const QByteArray& jsonData, bool replaceExisting, QString &errorMessage);
    QByteArray exportSitesToJson() const;

    static QVector<SitesPreset> sitesPresets(RouteMode routeMode);
    QStringList addedPresetIds() const;
    bool isPresetLoading(const QString &presetId) const;
    bool addPreset(const QString &presetId);
    bool removePreset(const QString &presetId);

signals:
    void presetAdded(const QString &presetId);
    void presetAddFailed(const QString &presetId);

private slots:
    void onHostResolved(const QHostInfo &hostInfo);

private:
    void fillSites();
    bool addSiteInternal(const QString &hostname, const QStringList &ips);
    QString normalizeHostname(const QString &hostname) const;
    bool validateHostname(const QString &hostname) const;
    void processSiteAfterResolve(const QString &hostname, const QStringList &ips);
    void processSite(const QString &hostname, const QStringList &ips);
    bool parseSitesJson(const QByteArray &jsonData, QMap<QString, QStringList> &sites, QString &errorMessage) const;
    void applyPreset(const SitesPreset &preset, const QMap<QString, QStringList> &sites);

    SecureAppSettingsRepository* m_appSettingsRepository;
    RouteMode m_currentRouteMode;
    QVector<QPair<QString, QStringList>> m_sites;
    QSet<QString> m_loadingPresets;
    QNetworkAccessManager m_networkManager;
};

#endif // IPSPLITTUNNELINGCONTROLLER_H

