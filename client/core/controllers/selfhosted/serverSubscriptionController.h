#ifndef SERVERSUBSCRIPTIONCONTROLLER_H
#define SERVERSUBSCRIPTIONCONTROLLER_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

#include "core/controllers/selfhosted/importController.h"
#include "core/repositories/secureServersRepository.h"
#include "core/utils/errorCodes.h"

using namespace amnezia;

class ServerSubscriptionController : public QObject
{
    Q_OBJECT

public:
    explicit ServerSubscriptionController(SecureServersRepository *serversRepository, ImportController *importController,
                                          QObject *parent = nullptr);

    static bool isSubscriptionUrl(const QString &text);
    static QStringList extractLinks(const QByteArray &body);

    ErrorCode importSubscription(const QString &url);
    ErrorCode updateSubscription(const QString &subscriptionId);
    void removeSubscription(const QString &subscriptionId);
    QString subscriptionName(const QString &subscriptionId) const;

private:
    struct SubscriptionContent
    {
        QString title;
        QStringList links;
    };

    ErrorCode download(const QString &url, SubscriptionContent &content);
    ErrorCode apply(const QString &subscriptionId, const QString &url, const SubscriptionContent &content);

    SecureServersRepository *m_serversRepository;
    ImportController *m_importController;
    QNetworkAccessManager m_networkManager;
};

#endif // SERVERSUBSCRIPTIONCONTROLLER_H
