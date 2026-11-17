#ifndef XRAY_H
#define XRAY_H

#include <QElapsedTimer>
#include <QString>
#include <QTimer>

#include <atomic>

class Xray
{
public:
    static Xray& getInstance()
    {
        static Xray instance;
        return instance;
    }

    // The engine is shared between consumers (VPN protocol, local proxy).
    // start() returns an owner token; stop(token) is a no-op unless the token
    // still owns the running engine, so a late teardown can't kill a newer owner.
    qint64 start(const QString& cfg);
    bool stop(qint64 token);
    bool stopAny();
    qint64 currentToken() const;

private:
    Xray();

    bool startXray(const QString& cfg);
    bool stopXray();
    void logPulse();

    qint64 m_currentToken = 0;
    qint64 m_lastToken = 0;

    QTimer m_pulseTimer;
    QElapsedTimer m_runningTimer;
    std::atomic<quint64> m_socketsOpened { 0 };
    quint64 m_warningLines = 0;
    quint64 m_errorLines = 0;

    static void ctxSockCallback(uintptr_t fd, void* ctx) {
        reinterpret_cast<Xray*>(ctx)->sockCallback(fd);
    }
    static void ctxLogHandler(char* str, void* ctx) {
        reinterpret_cast<Xray*>(ctx)->logHandler(str);
    }

    void sockCallback(uintptr_t fd);
    void logHandler(char* str);

#ifdef Q_OS_LINUX
    QByteArray m_defaultIfaceName;
#else
    int m_defaultIfaceIdx;
#endif

#ifdef Q_OS_MAC
    QString m_uplinkIfaceName;
    QString m_uplinkGateway;
#endif
};

#endif // XRAY_H
