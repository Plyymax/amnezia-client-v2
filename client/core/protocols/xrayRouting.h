#ifndef XRAYROUTING_H
#define XRAYROUTING_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace amnezia::xrayRouting
{
    QString proxyOutboundTag(QJsonArray &outbounds);

    void routeOnlySitesThroughProxy(QJsonObject &xrayConfig, const QJsonArray &domains, const QJsonArray &networks);

    void blockLocalNetworkNoise(QJsonObject &xrayConfig);

    void resolveDnsInsideCore(QJsonObject &xrayConfig, const QStringList &dnsServers);
}

#endif // XRAYROUTING_H
