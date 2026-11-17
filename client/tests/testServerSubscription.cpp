#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTest>
#include <QUuid>

#include "core/controllers/selfhosted/serverSubscriptionController.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/serialization/serialization.h"
#include "secureQSettings.h"
#include "utils/testCoreController.h"
#include "vpnConnection.h"

using namespace amnezia;

namespace
{
QString vlessLink(const QString &host, const QString &remark, const QString &extraQuery = QString())
{
    return QStringLiteral("vless://00000000-0000-4000-8000-000000000000@") + host
            + QStringLiteral(":443?encryption=none&security=reality&sni=example.com&fp=chrome&pbk=testPublicKey"
                             "&sid=abcd&type=xhttp&path=%2Fxh&mode=auto")
            + extraQuery + QLatin1Char('#') + remark;
}
} // namespace

class TestServerSubscription : public QObject
{
    Q_OBJECT

private:
    TestCoreController *m_coreController = nullptr;
    SecureQSettings *m_settings = nullptr;
    ServerSubscriptionController *m_subscriptionController = nullptr;

    QHttpServer m_httpServer;
    QTcpServer *m_tcpServer = nullptr;
    QByteArray m_subscriptionBody;
    QByteArray m_subscriptionTitle;

    QString subscriptionUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1/sub").arg(m_tcpServer->serverPort());
    }

    QStringList subscriptionServerIds() const
    {
        QStringList ids;
        for (const QString &serverId : m_coreController->m_serversRepository->orderedServerIds()) {
            const auto config = m_coreController->m_serversRepository->nativeConfig(serverId);
            if (config && config->isFromSubscription()) {
                ids.append(serverId);
            }
        }
        return ids;
    }

    void setSubscriptionLinks(const QStringList &links)
    {
        m_subscriptionBody = links.join(QLatin1Char('\n')).toUtf8().toBase64();
    }

private slots:
    void initTestCase()
    {
        const QString testOrg = "AmneziaVPN-Test-" + QUuid::createUuid().toString();
        m_settings = new SecureQSettings(testOrg, "amnezia-client", nullptr, false);

        auto vpnConnection = QSharedPointer<VpnConnection>::create(nullptr, nullptr);
        m_coreController = new TestCoreController(vpnConnection, m_settings, nullptr, this);
        m_subscriptionController = new ServerSubscriptionController(m_coreController->m_serversRepository,
                                                                    m_coreController->m_importCoreController, this);

        m_tcpServer = new QTcpServer(this);
        QVERIFY(m_tcpServer->listen(QHostAddress::LocalHost));
        QVERIFY(m_httpServer.bind(m_tcpServer));
        m_httpServer.route("/sub", [this]() {
            QHttpServerResponse response(QByteArrayLiteral("text/plain"), m_subscriptionBody);
            QHttpHeaders headers = response.headers();
            if (!m_subscriptionTitle.isEmpty()) {
                headers.append(QByteArrayLiteral("profile-title"), m_subscriptionTitle);
            }
            response.setHeaders(std::move(headers));
            return response;
        });
    }

    void cleanupTestCase()
    {
        m_settings->clearSettings();
        delete m_subscriptionController;
        delete m_coreController;
        delete m_settings;
    }

    void init()
    {
        m_coreController->m_serversRepository->clearServers();
        m_subscriptionTitle.clear();
        m_subscriptionBody.clear();
    }

    void testIsSubscriptionUrl()
    {
        QVERIFY(ServerSubscriptionController::isSubscriptionUrl("https://example.com/sub/abc"));
        QVERIFY(ServerSubscriptionController::isSubscriptionUrl("  http://example.com/sub  "));
        QVERIFY(!ServerSubscriptionController::isSubscriptionUrl("vpn://AAAA"));
        QVERIFY(!ServerSubscriptionController::isSubscriptionUrl(vlessLink("192.0.2.1", "a")));
        QVERIFY(!ServerSubscriptionController::isSubscriptionUrl("example.com"));
    }

    void testExtractLinks()
    {
        const QStringList links = { vlessLink("192.0.2.1", "one"), "trojan://secret@192.0.2.2:443#two" };
        const QByteArray plain = links.join("\r\n").toUtf8();

        QCOMPARE(ServerSubscriptionController::extractLinks(plain), links);
        QCOMPARE(ServerSubscriptionController::extractLinks(plain.toBase64()), links);

        QByteArray urlSafeNoPadding =
                plain.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        QCOMPARE(ServerSubscriptionController::extractLinks(urlSafeNoPadding), links);

        QByteArray wrapped = plain.toBase64();
        wrapped.insert(20, "\n");
        QCOMPARE(ServerSubscriptionController::extractLinks(wrapped), links);

        QVERIFY(ServerSubscriptionController::extractLinks("<html>not a subscription</html>").isEmpty());
    }

    void testVlessXhttpDeserialize()
    {
        const QString link = vlessLink("192.0.2.1", "xh",
                                       QStringLiteral("&host=cdn.example.com&extra=%7B%22xPaddingBytes%22%3A%22100-1000%22%7D"));
        QString alias;
        QString error;
        const QJsonObject config = serialization::vless::Deserialize(link, &alias, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(alias, QStringLiteral("xh"));

        const QJsonObject stream =
                config.value("outbounds").toArray().first().toObject().value("streamSettings").toObject();
        QCOMPARE(stream.value("network").toString(), QStringLiteral("xhttp"));
        QCOMPARE(stream.value("security").toString(), QStringLiteral("reality"));

        const QJsonObject xhttp = stream.value("xhttpSettings").toObject();
        QCOMPARE(xhttp.value("path").toString(), QStringLiteral("/xh"));
        QCOMPARE(xhttp.value("host").toString(), QStringLiteral("cdn.example.com"));
        QCOMPARE(xhttp.value("mode").toString(), QStringLiteral("auto"));
        QCOMPARE(xhttp.value("extra").toObject().value("xPaddingBytes").toString(), QStringLiteral("100-1000"));

        const QJsonObject reality = stream.value("realitySettings").toObject();
        QCOMPARE(reality.value("publicKey").toString(), QStringLiteral("testPublicKey"));
        QCOMPARE(reality.value("shortId").toString(), QStringLiteral("abcd"));
    }

    void testImportSubscription()
    {
        m_subscriptionTitle = "base64:" + QByteArray("My VPN").toBase64();
        setSubscriptionLinks({ vlessLink("192.0.2.1", "one"), vlessLink("192.0.2.2", "two"), "hysteria2://x@192.0.2.9:443#skip" });

        QCOMPARE(m_subscriptionController->importSubscription(subscriptionUrl()), ErrorCode::NoError);

        const QStringList ids = subscriptionServerIds();
        QCOMPARE(ids.size(), 2);

        const auto first = m_coreController->m_serversRepository->nativeConfig(ids.at(0));
        QVERIFY(first.has_value());
        QCOMPARE(first->displayName, QStringLiteral("one"));
        QCOMPARE(first->subscriptionName, QStringLiteral("My VPN"));
        QCOMPARE(first->subscriptionUrl, subscriptionUrl());
        QCOMPARE(m_subscriptionController->subscriptionName(first->subscriptionId), QStringLiteral("My VPN"));
        QCOMPARE(m_coreController->m_serversRepository->defaultServerId(), ids.at(0));
    }

    void testReimportUpdatesSameSubscription()
    {
        setSubscriptionLinks({ vlessLink("192.0.2.1", "one") });
        QCOMPARE(m_subscriptionController->importSubscription(subscriptionUrl()), ErrorCode::NoError);
        QCOMPARE(m_subscriptionController->importSubscription(subscriptionUrl()), ErrorCode::NoError);
        QCOMPARE(subscriptionServerIds().size(), 1);
    }

    void testUpdateSubscription()
    {
        setSubscriptionLinks({ vlessLink("192.0.2.1", "one"), vlessLink("192.0.2.2", "two") });
        QCOMPARE(m_subscriptionController->importSubscription(subscriptionUrl()), ErrorCode::NoError);

        const QStringList importedIds = subscriptionServerIds();
        const QString subscriptionId =
                m_coreController->m_serversRepository->nativeConfig(importedIds.at(0))->subscriptionId;

        QJsonObject manualServer = m_coreController->m_serversRepository->serverJsonById(importedIds.at(0)).value();
        manualServer.remove(configKey::storageServerId);
        manualServer.remove(configKey::subscriptionId);
        manualServer.remove(configKey::subscriptionName);
        manualServer.remove(configKey::subscriptionUrl);
        manualServer.remove(configKey::subscriptionLink);
        const QString manualServerId = m_coreController->m_serversRepository->addServer(
                QString(), manualServer, serverConfigUtils::ConfigType::Native);

        m_coreController->m_serversRepository->setDefaultServer(importedIds.at(1));

        setSubscriptionLinks({ vlessLink("192.0.2.2", "two"), vlessLink("192.0.2.3", "three") });
        QCOMPARE(m_subscriptionController->updateSubscription(subscriptionId), ErrorCode::NoError);

        const QStringList updatedIds = subscriptionServerIds();
        QCOMPARE(updatedIds.size(), 2);
        QCOMPARE(updatedIds.at(0), importedIds.at(1));
        QCOMPARE(m_coreController->m_serversRepository->nativeConfig(updatedIds.at(1))->displayName, QStringLiteral("three"));
        QCOMPARE(m_coreController->m_serversRepository->defaultServerId(), importedIds.at(1));

        const QVector<QString> order = m_coreController->m_serversRepository->orderedServerIds();
        QCOMPARE(order.size(), 3);
        QCOMPARE(order.last(), manualServerId);
    }

    void testEmptySubscriptionKeepsServers()
    {
        setSubscriptionLinks({ vlessLink("192.0.2.1", "one") });
        QCOMPARE(m_subscriptionController->importSubscription(subscriptionUrl()), ErrorCode::NoError);
        const QString subscriptionId =
                m_coreController->m_serversRepository->nativeConfig(subscriptionServerIds().first())->subscriptionId;

        m_subscriptionBody = "not a subscription";
        QCOMPARE(m_subscriptionController->updateSubscription(subscriptionId), ErrorCode::ImportSubscriptionEmptyError);
        QCOMPARE(subscriptionServerIds().size(), 1);
    }

    void testRealSubscription()
    {
        const QString url = qEnvironmentVariable("THIRD_PARTY_SUBSCRIPTION_URL");
        if (url.isEmpty()) {
            QSKIP("THIRD_PARTY_SUBSCRIPTION_URL is not configured");
        }

        QCOMPARE(m_subscriptionController->importSubscription(url), ErrorCode::NoError);
        const QStringList ids = subscriptionServerIds();
        QVERIFY(!ids.isEmpty());

        for (const QString &serverId : ids) {
            const auto config = m_coreController->m_serversRepository->nativeConfig(serverId);
            QVERIFY(config.has_value());
            QVERIFY(!config->subscriptionName.isEmpty());
            QVERIFY(config->containers.contains(DockerContainer::Xray) || config->containers.contains(DockerContainer::SSXray));

            if (config->subscriptionLink.contains(QLatin1String("type=xhttp"))) {
                const QJsonObject serverJson = m_coreController->m_serversRepository->serverJsonById(serverId).value();
                QVERIFY(QJsonDocument(serverJson).toJson().contains("xhttpSettings"));
            }
        }
    }

    void testDownloadError()
    {
        const QString missingUrl = QStringLiteral("http://127.0.0.1:%1/missing").arg(m_tcpServer->serverPort());
        QCOMPARE(m_subscriptionController->importSubscription(missingUrl), ErrorCode::ImportSubscriptionDownloadError);
        QVERIFY(subscriptionServerIds().isEmpty());
    }
};

QTEST_MAIN(TestServerSubscription)
#include "testServerSubscription.moc"
