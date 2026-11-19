#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "core/protocols/xrayRouting.h"

using namespace amnezia;

class TestXrayRouting : public QObject
{
    Q_OBJECT

private:
    static QJsonObject vlessConfig()
    {
        const QJsonObject vnext { { "address", "example.org" }, { "port", 443 } };
        const QJsonObject outbound { { "protocol", "vless" },
                                     { "settings", QJsonObject { { "vnext", QJsonArray { vnext } } } } };
        const QJsonObject inbound { { "protocol", "socks" }, { "port", 10808 } };
        return { { "inbounds", QJsonArray { inbound } }, { "outbounds", QJsonArray { outbound } } };
    }

    static QJsonArray rules(const QJsonObject &config)
    {
        return config.value("routing").toObject().value("rules").toArray();
    }

private slots:
    void proxyOutboundGetsTagWhenMissing()
    {
        QJsonArray outbounds = vlessConfig().value("outbounds").toArray();
        QCOMPARE(xrayRouting::proxyOutboundTag(outbounds), QString("proxy"));
        QCOMPARE(outbounds.first().toObject().value("tag").toString(), QString("proxy"));
    }

    void outboundWithoutServerIsNotProxy()
    {
        QJsonArray outbounds { QJsonObject { { "protocol", "freedom" } } };
        QVERIFY(xrayRouting::proxyOutboundTag(outbounds).isEmpty());
    }

    void sitesRulesComeBeforeOwnRulesAndCatchAllIsLast()
    {
        QJsonObject config = vlessConfig();
        config["routing"] = QJsonObject { { "rules", QJsonArray { QJsonObject { { "type", "field" }, { "outboundTag", "block" } } } } };

        xrayRouting::routeOnlySitesThroughProxy(config, QJsonArray { "*.Example.com", " " }, QJsonArray { "1.2.3.0/24", "" });

        const QJsonArray result = rules(config);
        QCOMPARE(result.size(), 4);
        QCOMPARE(result.at(0).toObject().value("domain").toArray(), QJsonArray { "domain:example.com" });
        QCOMPARE(result.at(1).toObject().value("ip").toArray(), QJsonArray { "1.2.3.0/24" });
        QCOMPARE(result.at(2).toObject().value("outboundTag").toString(), QString("block"));
        QCOMPARE(result.at(3).toObject().value("outboundTag").toString(), QString("direct"));
        QCOMPARE(result.at(3).toObject().value("network").toString(), QString("tcp,udp"));
    }

    void sitesRoutingEnablesRouteOnlySniffing()
    {
        QJsonObject config = vlessConfig();
        xrayRouting::routeOnlySitesThroughProxy(config, QJsonArray { "example.com" }, {});

        const QJsonObject sniffing = config.value("inbounds").toArray().first().toObject().value("sniffing").toObject();
        QVERIFY(sniffing.value("enabled").toBool());
        QVERIFY(sniffing.value("routeOnly").toBool());
    }

    void sitesRoutingLeavesConfigWithoutProxyUntouched()
    {
        QJsonObject config { { "outbounds", QJsonArray { QJsonObject { { "protocol", "freedom" } } } } };
        const QJsonObject before = config;
        xrayRouting::routeOnlySitesThroughProxy(config, QJsonArray { "example.com" }, {});
        QCOMPARE(config, before);
    }

    void dnsIsHijackedOnlyForClientInboundsAndComesFirst()
    {
        QJsonObject config = vlessConfig();
        xrayRouting::routeOnlySitesThroughProxy(config, QJsonArray { "example.com" }, QJsonArray { "1.1.1.1" });
        xrayRouting::resolveDnsInsideCore(config, { "1.1.1.1", "", "1.0.0.1", "1.1.1.1" });

        const QJsonObject dnsRule = rules(config).first().toObject();
        QCOMPARE(dnsRule.value("outboundTag").toString(), QString("dns-out"));
        QCOMPARE(dnsRule.value("port").toString(), QString("53"));
        QCOMPARE(dnsRule.value("network").toString(), QString("udp"));

        const QString inboundTag = config.value("inbounds").toArray().first().toObject().value("tag").toString();
        QVERIFY(!inboundTag.isEmpty());
        QCOMPARE(dnsRule.value("inboundTag").toArray(), QJsonArray { inboundTag });

        const QJsonObject dns = config.value("dns").toObject();
        QCOMPARE(dns.value("servers").toArray(), (QJsonArray { "1.1.1.1", "1.0.0.1" }));
        QCOMPARE(dns.value("queryStrategy").toString(), QString("UseIPv4"));

        int dnsOutbounds = 0;
        for (const QJsonValue &outbound : config.value("outbounds").toArray()) {
            dnsOutbounds += outbound.toObject().value("protocol").toString() == QLatin1String("dns");
        }
        QCOMPARE(dnsOutbounds, 1);
    }

    void dnsHijackKeepsExistingDnsServersAndInboundTags()
    {
        QJsonObject config = vlessConfig();
        QJsonArray inbounds = config.value("inbounds").toArray();
        QJsonObject inbound = inbounds.first().toObject();
        inbound["tag"] = "socks-in";
        inbounds[0] = inbound;
        config["inbounds"] = inbounds;
        config["dns"] = QJsonObject { { "servers", QJsonArray { "8.8.8.8" } }, { "hosts", QJsonObject { { "a.b", "1.2.3.4" } } } };

        xrayRouting::resolveDnsInsideCore(config, { "1.1.1.1" });

        QCOMPARE(rules(config).first().toObject().value("inboundTag").toArray(), QJsonArray { "socks-in" });
        QCOMPARE(config.value("dns").toObject().value("servers").toArray(), QJsonArray { "8.8.8.8" });
        QVERIFY(config.value("dns").toObject().contains("hosts"));
    }

    void dnsHijackIsIdempotentForOutboundAndSkippedWithoutServers()
    {
        QJsonObject config = vlessConfig();
        xrayRouting::resolveDnsInsideCore(config, { "", "not-an-ip" });
        QVERIFY(rules(config).isEmpty());
        QVERIFY(!config.contains("dns"));

        xrayRouting::resolveDnsInsideCore(config, { "1.1.1.1" });
        const int outboundsAfterFirst = config.value("outbounds").toArray().size();
        xrayRouting::resolveDnsInsideCore(config, { "1.1.1.1" });
        QCOMPARE(config.value("outbounds").toArray().size(), outboundsAfterFirst);
    }

    void applyingToCopyDoesNotGrowOriginal()
    {
        const QJsonObject original = vlessConfig();
        for (int i = 0; i < 3; ++i) {
            QJsonObject copy = original;
            xrayRouting::routeOnlySitesThroughProxy(copy, QJsonArray { "example.com" }, {});
            xrayRouting::resolveDnsInsideCore(copy, { "1.1.1.1" });
            QCOMPARE(rules(copy).size(), 3);
        }
    }
};

QTEST_GUILESS_MAIN(TestXrayRouting)
#include "testXrayRouting.moc"
