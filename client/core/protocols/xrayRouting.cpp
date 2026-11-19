#include "xrayRouting.h"

#include <QHostAddress>

namespace
{
    const QString dnsOutboundTag = QStringLiteral("dns-out");
    const QString blockOutboundTag = QStringLiteral("block-local");

    QString directOutboundTag(QJsonArray &outbounds)
    {
        for (const QJsonValue &value : outbounds) {
            const QJsonObject outbound = value.toObject();
            if (outbound.value("protocol").toString() == QLatin1String("freedom") && !outbound.value("tag").toString().isEmpty()) {
                return outbound.value("tag").toString();
            }
        }
        outbounds.append(QJsonObject { { "protocol", "freedom" }, { "tag", "direct" } });
        return "direct";
    }

    void enableSniffing(QJsonObject &xrayConfig)
    {
        QJsonArray inbounds = xrayConfig.value("inbounds").toArray();
        for (int i = 0; i < inbounds.size(); ++i) {
            QJsonObject inbound = inbounds.at(i).toObject();
            inbound.insert("sniffing", QJsonObject { { "enabled", true },
                                                     { "destOverride", QJsonArray { "http", "tls", "quic" } },
                                                     { "routeOnly", true } });
            inbounds[i] = inbound;
        }
        xrayConfig.insert("inbounds", inbounds);
    }

    QJsonArray tagInbounds(QJsonObject &xrayConfig)
    {
        QJsonArray inbounds = xrayConfig.value("inbounds").toArray();
        QJsonArray tags;
        for (int i = 0; i < inbounds.size(); ++i) {
            QJsonObject inbound = inbounds.at(i).toObject();
            if (inbound.value("tag").toString().isEmpty()) {
                inbound.insert("tag", QStringLiteral("inbound-%1").arg(i));
                inbounds[i] = inbound;
            }
            tags.append(inbound.value("tag").toString());
        }
        xrayConfig.insert("inbounds", inbounds);
        return tags;
    }

    bool hasOutboundTag(const QJsonArray &outbounds, const QString &tag)
    {
        for (const QJsonValue &value : outbounds) {
            if (value.toObject().value("tag").toString() == tag) {
                return true;
            }
        }
        return false;
    }

    QJsonArray prependRules(const QJsonArray &first, const QJsonArray &rest)
    {
        QJsonArray rules = first;
        for (const QJsonValue &rule : rest) {
            rules.append(rule);
        }
        return rules;
    }
}

namespace amnezia::xrayRouting
{
    QString proxyOutboundTag(QJsonArray &outbounds)
    {
        for (int i = 0; i < outbounds.size(); ++i) {
            QJsonObject outbound = outbounds.at(i).toObject();
            const QJsonObject settings = outbound.value("settings").toObject();
            if (!settings.contains("vnext") && !settings.contains("servers")) {
                continue;
            }
            if (outbound.value("tag").toString().isEmpty()) {
                outbound.insert("tag", "proxy");
                outbounds[i] = outbound;
            }
            return outbound.value("tag").toString();
        }
        return {};
    }

    void routeOnlySitesThroughProxy(QJsonObject &xrayConfig, const QJsonArray &domains, const QJsonArray &networks)
    {
        QJsonArray outbounds = xrayConfig.value("outbounds").toArray();
        const QString proxyTag = proxyOutboundTag(outbounds);
        if (proxyTag.isEmpty()) {
            return;
        }
        const QString directTag = directOutboundTag(outbounds);
        xrayConfig.insert("outbounds", outbounds);

        enableSniffing(xrayConfig);

        QJsonArray domainMatchers;
        for (const QJsonValue &value : domains) {
            QString domain = value.toString().trimmed().toLower();
            if (domain.startsWith(QLatin1String("*."))) {
                domain = domain.mid(2);
            }
            if (!domain.isEmpty()) {
                domainMatchers.append(QStringLiteral("domain:") + domain);
            }
        }

        QJsonArray networkMatchers;
        for (const QJsonValue &value : networks) {
            const QString network = value.toString().trimmed();
            if (!network.isEmpty()) {
                networkMatchers.append(network);
            }
        }

        QJsonArray ownRules;
        if (!domainMatchers.isEmpty()) {
            ownRules.append(QJsonObject { { "type", "field" }, { "domain", domainMatchers }, { "outboundTag", proxyTag } });
        }
        if (!networkMatchers.isEmpty()) {
            ownRules.append(QJsonObject { { "type", "field" }, { "ip", networkMatchers }, { "outboundTag", proxyTag } });
        }

        QJsonObject routing = xrayConfig.value("routing").toObject();
        QJsonArray rules = prependRules(ownRules, routing.value("rules").toArray());
        rules.append(QJsonObject { { "type", "field" }, { "network", "tcp,udp" }, { "outboundTag", directTag } });
        routing.insert("rules", rules);
        xrayConfig.insert("routing", routing);
    }

    void blockLocalNetworkNoise(QJsonObject &xrayConfig)
    {
        QJsonArray outbounds = xrayConfig.value("outbounds").toArray();
        if (!hasOutboundTag(outbounds, blockOutboundTag)) {
            outbounds.append(QJsonObject { { "protocol", "blackhole" }, { "tag", blockOutboundTag } });
            xrayConfig.insert("outbounds", outbounds);
        }

        const QJsonArray noiseRules {
            QJsonObject { { "type", "field" },
                          { "network", "udp" },
                          { "port", "135,137-139,5353" },
                          { "outboundTag", blockOutboundTag } },
            QJsonObject { { "type", "field" },
                          { "ip", QJsonArray { "224.0.0.0/3", "ff00::/8" } },
                          { "outboundTag", blockOutboundTag } },
        };
        QJsonObject routing = xrayConfig.value("routing").toObject();
        routing.insert("rules", prependRules(noiseRules, routing.value("rules").toArray()));
        xrayConfig.insert("routing", routing);
    }

    void resolveDnsInsideCore(QJsonObject &xrayConfig, const QStringList &dnsServers)
    {
        QJsonArray servers;
        for (const QString &server : dnsServers) {
            if (!QHostAddress(server).isNull() && !servers.contains(server)) {
                servers.append(server);
            }
        }
        if (servers.isEmpty()) {
            return;
        }

        QJsonArray outbounds = xrayConfig.value("outbounds").toArray();
        if (!hasOutboundTag(outbounds, dnsOutboundTag)) {
            outbounds.append(QJsonObject { { "protocol", "dns" }, { "tag", dnsOutboundTag } });
            xrayConfig.insert("outbounds", outbounds);
        }

        QJsonObject dns = xrayConfig.value("dns").toObject();
        if (dns.value("servers").toArray().isEmpty()) {
            dns.insert("servers", servers);
        }
        if (!dns.contains("queryStrategy")) {
            dns.insert("queryStrategy", "UseIPv4");
        }
        xrayConfig.insert("dns", dns);

        const QJsonArray dnsRule { QJsonObject { { "type", "field" },
                                                 { "inboundTag", tagInbounds(xrayConfig) },
                                                 { "network", "udp" },
                                                 { "port", "53" },
                                                 { "outboundTag", dnsOutboundTag } } };
        QJsonObject routing = xrayConfig.value("routing").toObject();
        routing.insert("rules", prependRules(dnsRule, routing.value("rules").toArray()));
        xrayConfig.insert("routing", routing);
    }
}
