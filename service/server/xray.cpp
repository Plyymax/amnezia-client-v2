#include "xray.h"
#include "core/utils/constants/protocolConstants.h"
#include "core/utils/networkUtilities.h"
#ifdef Q_OS_MAC
#include "router_mac.h"
#endif

#include <QDebug>
#include <QNetworkInterface>
#include <QCoreApplication>
#include <amnezia_xray.h>
#include <qdebug.h>

#ifdef Q_OS_DARWIN
    #include <arpa/inet.h>
    #include <cerrno>
    #include <cstddef>
    #include <cstdint>
    #include <cstring>
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <sys/socket.h>
#endif
#ifdef Q_OS_WIN
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif
#ifdef Q_OS_LINUX
    #include <sys/socket.h>
    #include "xray_defs.h"
#endif

namespace
{
    constexpr int pulseIntervalMs = 60000;
#ifdef Q_OS_WIN
    constexpr int uplinkCheckIntervalMs = 5000;
#endif
}

Xray::Xray()
{
    m_pulseTimer.setInterval(pulseIntervalMs);
    QObject::connect(&m_pulseTimer, &QTimer::timeout, [this]() { logPulse(); });
#ifdef Q_OS_WIN
    m_uplinkTimer.setInterval(uplinkCheckIntervalMs);
    QObject::connect(&m_uplinkTimer, &QTimer::timeout, [this]() { refreshUplink(); });
#endif
}

qint64 Xray::start(const QString &cfg)
{
    if (m_currentToken != 0) {
        qDebug() << "Xray::start() engine is busy (token" << m_currentToken << "), stopping it for the new owner";
        stopXray();
        m_currentToken = 0;
    }

    if (!startXray(cfg)) {
        return 0;
    }

    m_currentToken = ++m_lastToken;
    qDebug() << "Xray::start() engine started, owner token" << m_currentToken;

    m_socketsOpened = 0;
    m_warningLines = 0;
    m_errorLines = 0;
    m_runningTimer.start();
    m_pulseTimer.start();
#ifdef Q_OS_WIN
    m_uplinkTimer.start();
#endif
    return m_currentToken;
}

bool Xray::stop(qint64 token)
{
    if (m_currentToken == 0) {
        return true;
    }

    if (token == 0 || token != m_currentToken) {
        qDebug() << "Xray::stop() stale owner token" << token << "(current" << m_currentToken << "), ignoring";
        return true;
    }

    m_currentToken = 0;
    return stopXray();
}

bool Xray::stopAny()
{
    if (m_currentToken == 0) {
        return true;
    }

    m_currentToken = 0;
    return stopXray();
}

qint64 Xray::currentToken() const
{
    return m_currentToken;
}

bool Xray::startXray(const QString &cfg)
{
    qDebug() << "Xray::startXray()";
    const auto gatewayAndIface = NetworkUtilities::getGatewayAndIface();
    const QString defaultGateway = gatewayAndIface.first;
    const QNetworkInterface defaultIface = gatewayAndIface.second;
#ifdef Q_OS_LINUX
    m_defaultIfaceName = defaultIface.name().toUtf8();
#else
    m_defaultIfaceIdx = defaultIface.index();
#endif
    if (defaultIface.index() > 0) {
        qDebug() << "[xray] using uplink interface:" << defaultIface.name() << "(" << defaultIface.index() << ")";
    }

#ifdef Q_OS_MAC
    m_uplinkIfaceName = defaultIface.name();
    m_uplinkGateway = defaultGateway;
    if (!m_uplinkIfaceName.isEmpty()) {
        const bool installed = RouterMac::Instance().routeAddXray(m_uplinkIfaceName, m_uplinkGateway);
        if (!installed) {
            qWarning() << "[xray] failed to install xray routes on" << m_uplinkIfaceName;
        }
    }
#endif

    if (auto err = amnezia_xray_setsockcallback(ctxSockCallback, this); err != nullptr) {
        qDebug() << "[xray] sockopt failed: " << err;
        amnezia_xray_free(err);
        return false;
    }

    amnezia_xray_setloghandler(ctxLogHandler, this);

    QByteArray bytes = cfg.toUtf8();
    if (auto err = amnezia_xray_configure(bytes.data()); err != nullptr) {
        qDebug() << "[xray] configuration failed: " << err;
        amnezia_xray_free(err);
        return false;
    }

    if (auto err = amnezia_xray_start(); err != nullptr) {
        qDebug() << "[xray] failed to start: " << err;
        amnezia_xray_free(err);
        return false;
    }

    return true;
}

bool Xray::stopXray()
{
    qInfo().noquote() << QString("[xray] stopping the core after %1 s").arg(m_runningTimer.isValid() ? m_runningTimer.elapsed() / 1000 : 0);
    m_pulseTimer.stop();
#ifdef Q_OS_WIN
    m_uplinkTimer.stop();
#endif
    m_runningTimer.invalidate();
    bool success = true;
    if (auto err = amnezia_xray_stop(); err != nullptr) {
        qDebug() << "[xray] failed to stop: " << err;
        amnezia_xray_free(err);
        success = false;
    }

#ifdef Q_OS_MAC
    if (!m_uplinkIfaceName.isEmpty()) {
        RouterMac::Instance().routeDeleteXray(m_uplinkIfaceName, m_uplinkGateway);
    }
    m_uplinkIfaceName.clear();
    m_uplinkGateway.clear();
#endif

    return success;
}

void Xray::logHandler(char* str)
{
    QMetaObject::invokeMethod(qApp, [this, str = QString::fromUtf8(str)] {
        if (str.contains("[Error]")) {
            ++m_errorLines;
        } else if (str.contains("[Warning]")) {
            ++m_warningLines;
        }
        qDebug() << "[xray]" << str;
    }, Qt::QueuedConnection);
}

#ifdef Q_OS_WIN
void Xray::refreshUplink()
{
    const QNetworkInterface candidate = NetworkUtilities::getGatewayAndIface().second;
    const int index = candidate.index();
    if (index <= 0 || index == m_defaultIfaceIdx.load()) {
        return;
    }

    for (const QNetworkAddressEntry &entry : candidate.addressEntries()) {
        if (entry.ip().toString() == amnezia::protocols::xray::defaultLocalAddr) {
            return;
        }
    }

    qInfo().noquote() << QString("[xray] uplink interface changed to %1 (%2)").arg(candidate.name()).arg(index);
    m_defaultIfaceIdx = index;
}
#endif

void Xray::logPulse()
{
    qInfo().noquote() << QString("[pulse] xray core running for %1 s, outbound sockets opened in the last %2 s: %3, core warnings: %4, errors: %5")
                                 .arg(m_runningTimer.elapsed() / 1000)
                                 .arg(pulseIntervalMs / 1000)
                                 .arg(m_socketsOpened.exchange(0))
                                 .arg(m_warningLines)
                                 .arg(m_errorLines);
    m_warningLines = 0;
    m_errorLines = 0;
}

void Xray::sockCallback(uintptr_t fd)
{
    ++m_socketsOpened;
#ifdef Q_OS_MAC
    if (int idx = m_defaultIfaceIdx; idx > 0) {
        setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &idx, sizeof(idx));
        setsockopt(fd, IPPROTO_IPV6, IPV6_BOUND_IF, &idx, sizeof(idx));
    }
#endif
#ifdef Q_OS_WIN
    if (DWORD idx = m_defaultIfaceIdx; idx > 0) {
        setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<char *>(&idx), sizeof(idx));
        idx = htonl(idx); // IP_UNICAST_IF expects index in network byte order
        setsockopt(fd, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<char *>(&idx), sizeof(idx));
    }
#endif
#ifdef Q_OS_LINUX
    if (!m_defaultIfaceName.isEmpty()) {
        setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, m_defaultIfaceName.data(), m_defaultIfaceName.size());
        setsockopt(fd, SOL_SOCKET, SO_MARK, &amnezia::xray::xrayTrafficMark, sizeof(amnezia::xray::xrayTrafficMark));
    }
#endif
}
