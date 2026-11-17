#ifndef IMPORTUICONTROLLER_H
#define IMPORTUICONTROLLER_H

#include <QObject>

#include "core/controllers/selfhosted/importController.h"
#include "core/controllers/selfhosted/serverSubscriptionController.h"

class ImportUiController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString config READ getConfig NOTIFY importConfigChanged)
    Q_PROPERTY(QString configFileName READ getConfigFileName NOTIFY importConfigChanged)
    Q_PROPERTY(QString maliciousWarningText READ getMaliciousWarningText NOTIFY importConfigChanged)
    Q_PROPERTY(bool isNativeWireGuardConfig READ isNativeWireGuardConfig NOTIFY importConfigChanged)

public:
    explicit ImportUiController(ImportController* importController, ServerSubscriptionController* serverSubscriptionController,
                                QObject *parent = nullptr);

public slots:
    void importConfig();
    void clearConfigFileName();
    bool extractConfigFromFile(const QString &fileName);
    bool extractConfigFromData(QString data);
    bool extractConfigFromQr(const QByteArray &data);
    QString getConfig();
    QString getConfigFileName();
    QString getMaliciousWarningText();
    bool isNativeWireGuardConfig();
    void processNativeWireGuardConfig();
    QString readTextFile(const QString &fileName);

    bool isSubscriptionUrl(const QString &text) const;
    bool importSubscription(const QString &url);
    bool updateSubscription(const QString &subscriptionId);
    void removeSubscription(const QString &subscriptionId);
    QString subscriptionName(const QString &subscriptionId) const;

#if defined Q_OS_ANDROID || defined Q_OS_IOS
    void startDecodingQr();
    bool parseQrCodeChunk(const QString &code);

    double getQrCodeScanProgressBarValue();
    QString getQrCodeScanProgressString();
#endif

#if defined Q_OS_ANDROID
    static bool decodeQrCode(const QString &code);
#endif

signals:
    void importFinished();
    void importErrorOccurred(ErrorCode errorCode, bool goToPageHome);
    void qrDecodingFinished();
    void restoreAppConfig(const QByteArray &data);
    void importConfigChanged();

private:
#if defined Q_OS_ANDROID || defined Q_OS_IOS
    void stopDecodingQr();
#endif

    ImportController* m_importController;
    ServerSubscriptionController* m_serverSubscriptionController;

    QJsonObject m_config;
    QString m_configFileName;
    QString m_maliciousWarningText;
    bool m_isNativeWireGuardConfig;

#if defined Q_OS_ANDROID
    static ImportUiController* mInstance;
#endif
};

#endif // IMPORTUICONTROLLER_H
