#include "serverSubscriptionController.h"

#include <QDebug>
#include <QEventLoop>
#include <QHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include "version.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/serverConfigUtils.h"

namespace
{
constexpr int subscriptionTransferTimeoutMs = 30000;

QString decodeProfileTitle(const QByteArray &header)
{
    const QByteArray base64TitlePrefix = QByteArrayLiteral("base64:");
    if (header.startsWith(base64TitlePrefix)) {
        return QString::fromUtf8(QByteArray::fromBase64(header.mid(base64TitlePrefix.size()))).trimmed();
    }
    return QUrl::fromPercentEncoding(header).trimmed();
}

QByteArray decodeBase64(const QByteArray &data)
{
    QByteArray compact = data.simplified();
    compact.replace(' ', QByteArray());
    while (compact.size() % 4 != 0) {
        compact.append('=');
    }

    for (const auto encoding : { QByteArray::Base64Encoding, QByteArray::Base64UrlEncoding }) {
        const auto decoded = QByteArray::fromBase64Encoding(compact, encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (decoded) {
            return *decoded;
        }
    }
    return {};
}

QJsonObject withoutStorageServerId(QJsonObject json)
{
    json.remove(configKey::storageServerId);
    return json;
}
} // namespace

ServerSubscriptionController::ServerSubscriptionController(SecureServersRepository *serversRepository,
                                                           ImportController *importController, QObject *parent)
    : QObject(parent), m_serversRepository(serversRepository), m_importController(importController)
{
}

bool ServerSubscriptionController::isSubscriptionUrl(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    return url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
            && !url.host().isEmpty();
}

QStringList ServerSubscriptionController::extractLinks(const QByteArray &body)
{
    const QByteArray trimmedBody = body.trimmed();
    QString text = QString::fromUtf8(trimmedBody);
    if (!text.contains(QLatin1String("://"))) {
        text = QString::fromUtf8(decodeBase64(trimmedBody));
    }

    QStringList links;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString link = line.trimmed();
        if (link.contains(QLatin1String("://"))) {
            links.append(link);
        }
    }
    links.removeDuplicates();
    return links;
}

ErrorCode ServerSubscriptionController::importSubscription(const QString &url)
{
    const QString subscriptionUrl = url.trimmed();

    SubscriptionContent content;
    const ErrorCode errorCode = download(subscriptionUrl, content);
    if (errorCode != ErrorCode::NoError) {
        return errorCode;
    }

    for (const QString &serverId : m_serversRepository->orderedServerIds()) {
        const auto config = m_serversRepository->nativeConfig(serverId);
        if (config && config->isFromSubscription() && config->subscriptionUrl == subscriptionUrl) {
            return apply(config->subscriptionId, subscriptionUrl, content);
        }
    }

    return apply(QUuid::createUuid().toString(QUuid::WithoutBraces), subscriptionUrl, content);
}

ErrorCode ServerSubscriptionController::updateSubscription(const QString &subscriptionId)
{
    for (const QString &serverId : m_serversRepository->orderedServerIds()) {
        const auto config = m_serversRepository->nativeConfig(serverId);
        if (config && config->subscriptionId == subscriptionId) {
            SubscriptionContent content;
            const ErrorCode errorCode = download(config->subscriptionUrl, content);
            if (errorCode != ErrorCode::NoError) {
                return errorCode;
            }
            return apply(subscriptionId, config->subscriptionUrl, content);
        }
    }
    return ErrorCode::ImportSubscriptionEmptyError;
}

void ServerSubscriptionController::removeSubscription(const QString &subscriptionId)
{
    for (const QString &serverId : m_serversRepository->orderedServerIds()) {
        const auto config = m_serversRepository->nativeConfig(serverId);
        if (config && config->subscriptionId == subscriptionId) {
            m_serversRepository->removeServer(serverId);
        }
    }
}

QString ServerSubscriptionController::subscriptionName(const QString &subscriptionId) const
{
    for (const QString &serverId : m_serversRepository->orderedServerIds()) {
        const auto config = m_serversRepository->nativeConfig(serverId);
        if (config && config->subscriptionId == subscriptionId) {
            return config->subscriptionName;
        }
    }
    return {};
}

ErrorCode ServerSubscriptionController::download(const QString &url, SubscriptionContent &content)
{
    QNetworkRequest request { QUrl(url) };
    request.setTransferTimeout(subscriptionTransferTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AmneziaVPN/%1").arg(QStringLiteral(APP_VERSION)));

    QNetworkReply *reply = m_networkManager.get(request);
    QEventLoop wait;
    connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Subscription download failed, network error:" << reply->error()
                   << "HTTP status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        return ErrorCode::ImportSubscriptionDownloadError;
    }

    content.title = decodeProfileTitle(reply->rawHeader(QByteArrayLiteral("profile-title")));
    content.links = extractLinks(reply->readAll());
    return content.links.isEmpty() ? ErrorCode::ImportSubscriptionEmptyError : ErrorCode::NoError;
}

ErrorCode ServerSubscriptionController::apply(const QString &subscriptionId, const QString &url,
                                              const SubscriptionContent &content)
{
    QHash<QString, QString> serverIdByLink;
    QSet<QString> staleServerIds;
    QString previousName;
    int insertPosition = -1;

    const QVector<QString> serverIds = m_serversRepository->orderedServerIds();
    for (int i = 0; i < serverIds.size(); ++i) {
        const auto config = m_serversRepository->nativeConfig(serverIds.at(i));
        if (!config || config->subscriptionId != subscriptionId) {
            continue;
        }
        if (!serverIdByLink.contains(config->subscriptionLink)) {
            serverIdByLink.insert(config->subscriptionLink, serverIds.at(i));
        }
        staleServerIds.insert(serverIds.at(i));
        previousName = config->subscriptionName;
        insertPosition = i + 1;
    }

    QString name = content.title;
    if (name.isEmpty()) {
        name = previousName.isEmpty() ? QUrl(url).host() : previousName;
    }

    QList<QPair<QString, QJsonObject>> servers;
    for (const QString &link : content.links) {
        const ImportController::ImportResult result = m_importController->extractConfigFromData(link);
        if (result.errorCode != ErrorCode::NoError
            || serverConfigUtils::configTypeFromJson(result.config) != serverConfigUtils::ConfigType::Native) {
            continue;
        }

        QJsonObject serverJson = result.config;
        serverJson.insert(configKey::subscriptionId, subscriptionId);
        serverJson.insert(configKey::subscriptionName, name);
        serverJson.insert(configKey::subscriptionUrl, url);
        serverJson.insert(configKey::subscriptionLink, link);
        servers.append({ link, serverJson });
    }

    if (servers.isEmpty()) {
        return ErrorCode::ImportSubscriptionEmptyError;
    }

    for (const auto &[link, serverJson] : servers) {
        const QString existingServerId = serverIdByLink.value(link);
        if (existingServerId.isEmpty()) {
            m_serversRepository->addServer(QString(), serverJson, serverConfigUtils::ConfigType::Native, insertPosition);
            if (insertPosition >= 0) {
                ++insertPosition;
            }
            continue;
        }

        staleServerIds.remove(existingServerId);
        const auto storedJson = m_serversRepository->serverJsonById(existingServerId);
        if (!storedJson || withoutStorageServerId(*storedJson) != serverJson) {
            m_serversRepository->editServer(existingServerId, serverJson, serverConfigUtils::ConfigType::Native);
        }
    }

    for (const QString &serverId : staleServerIds) {
        m_serversRepository->removeServer(serverId);
    }

    return ErrorCode::NoError;
}
