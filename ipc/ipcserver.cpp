#include "ipcserver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QString>
#include <QStringList>
#include <QTcpServer>

#include "logger.h"
#include "router.h"
#include "killswitch.h"
#include "xray.h"

#ifdef Q_OS_WIN
    #include "tapcontroller_win.h"
#endif


namespace
{
    const QDateTime serviceStartTime = QDateTime::currentDateTimeUtc();

    template<typename T>
    T logResult(const QString &call, T result)
    {
        if (result) {
            qInfo().noquote() << "[ipc]" << call << "-> ok";
        } else {
            qWarning().noquote() << "[ipc]" << call << "-> failed";
        }
        return result;
    }
}

IpcServer::IpcServer(QObject *parent) : IpcInterfaceSource(parent)
{
    connect(&m_pingHelper, &PingHelper::connectionLose, this, &IpcServer::connectionLose);
}

int IpcServer::createPrivilegedProcess()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::createPrivilegedProcess";
#endif

    m_localpid++;

    ProcessDescriptor pd(this);

    pd.localServer->setSocketOptions(QLocalServer::WorldAccessOption);

    if (!pd.localServer->listen(amnezia::getIpcProcessUrl(m_localpid))) {
        qDebug() << QString("Unable to start the server: %1.").arg(pd.localServer->errorString());
        return -1;
    }

    // Make sure any connections are handed to QtRO
    QObject::connect(pd.localServer.data(), &QLocalServer::newConnection, this, [pd]() {
        qDebug() << "IpcServer new connection";
        if (pd.serverNode) {
            pd.serverNode->addHostSideConnection(pd.localServer->nextPendingConnection());
            pd.serverNode->enableRemoting(pd.ipcProcess.data());
        }
    });

    QObject::connect(pd.serverNode.data(), &QRemoteObjectHost::error, this,
                     [pd](QRemoteObjectNode::ErrorCode errorCode) { qDebug() << "QRemoteObjectHost::error" << errorCode; });

    QObject::connect(pd.serverNode.data(), &QRemoteObjectHost::destroyed, this, [pd]() { qDebug() << "QRemoteObjectHost::destroyed"; });

    m_processes.insert(m_localpid, pd);

    return m_localpid;
}

int IpcServer::routeAddList(const QString &gw, const QStringList &ips)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::routeAddList";
#endif

    return Router::routeAddList(gw, ips);
}

bool IpcServer::clearSavedRoutes()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::clearSavedRoutes";
#endif

    return Router::clearSavedRoutes();
}

bool IpcServer::routeDeleteList(const QString &gw, const QStringList &ips)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::routeDeleteList";
#endif

    return Router::routeDeleteList(gw, ips);
}

bool IpcServer::flushDns()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::flushDns";
#endif

    return logResult("flushDns", Router::flushDns());
}

void IpcServer::resetIpStack()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::resetIpStack";
#endif

    Router::resetIpStack();
}

bool IpcServer::checkAndInstallDriver()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::checkAndInstallDriver";
#endif

#ifdef Q_OS_WIN
    return TapController::checkAndSetup();
#else
    return true;
#endif
}

QStringList IpcServer::getTapList()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::getTapList";
#endif

#ifdef Q_OS_WIN
    return TapController::getTapList();
#else
    return QStringList();
#endif
}

void IpcServer::cleanUp()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::cleanUp";
#endif

    Logger::deInit();
    Logger::cleanUp();
}

void IpcServer::clearLogs()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::clearLogs";
#endif

    Logger::clearLogs(true);
}

bool IpcServer::createTun(const QString &dev, const QString &subnet)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::createTun";
#endif

    return logResult("createTun " + dev, Router::createTun(dev, subnet));
}

bool IpcServer::deleteTun(const QString &dev)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::deleteTun";
#endif

    return logResult("deleteTun " + dev, Router::deleteTun(dev));
}

bool IpcServer::updateResolvers(const QString &ifname, const QList<QHostAddress> &resolvers)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::updateResolvers";
#endif

    return logResult(QString("updateResolvers %1, %2 servers").arg(ifname).arg(resolvers.size()), Router::updateResolvers(ifname, resolvers));
}

bool IpcServer::restoreResolvers()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::restoreResolvers";
#endif

    return logResult("restoreResolvers", Router::restoreResolvers());
}

bool IpcServer::StartRoutingIpv6()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::StartRoutingIpv6";
#endif

    return logResult("StartRoutingIpv6", Router::StartRoutingIpv6());
}

bool IpcServer::StopRoutingIpv6()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::StopRoutingIpv6";
#endif

    return logResult("StopRoutingIpv6", Router::StopRoutingIpv6());
}

void IpcServer::setLogsEnabled(bool enabled)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::setLogsEnabled";
#endif

    if (enabled == Logger::isEnabled()) {
        return;
    }

    if (enabled) {
        Logger::init(true);
        qInfo().noquote() << QString("[service] logging enabled, service pid %1 running since %2")
                                     .arg(QCoreApplication::applicationPid())
                                     .arg(serviceStartTime.toString(Qt::ISODate));
    } else {
        qInfo() << "[service] logging disabled";
        Logger::deInit();
    }
}

bool IpcServer::startNetworkCheck(const QString& serverIpv4Gateway, const QString& deviceIpv4Address)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::startNetworkCheck";
#endif

    qInfo() << "[ipc] startNetworkCheck";
    m_pingHelper.start(serverIpv4Gateway, deviceIpv4Address);
    return true;
}

bool IpcServer::stopNetworkCheck()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::stopNetworkCheck";
#endif

    qInfo() << "[ipc] stopNetworkCheck";
    m_pingHelper.stop();
    return true;
}

bool IpcServer::resetKillSwitchAllowedRange(QStringList ranges)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::resetKillSwitchAllowedRange";
#endif

    return logResult(QString("killswitch resetAllowedRange, %1 ranges").arg(ranges.size()), KillSwitch::instance()->resetAllowedRange(ranges));
}

bool IpcServer::addKillSwitchAllowedRange(QStringList ranges)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::addKillSwitchAllowedRange";
#endif

    return logResult(QString("killswitch addAllowedRange, %1 ranges").arg(ranges.size()), KillSwitch::instance()->addAllowedRange(ranges));
}

bool IpcServer::disableAllTraffic()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::disableAllTraffic";
#endif

    return logResult("killswitch disableAllTraffic (all traffic blocked)", KillSwitch::instance()->disableAllTraffic());
}

bool IpcServer::enableKillSwitch(const QJsonObject &configStr, int vpnAdapterIndex)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::enableKillSwitch";
#endif

    return logResult(QString("killswitch enable, vpn adapter %1").arg(vpnAdapterIndex),
                     KillSwitch::instance()->enableKillSwitch(configStr, vpnAdapterIndex));
}

bool IpcServer::disableKillSwitch()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::disableKillSwitch";
#endif

    return logResult("killswitch disable", KillSwitch::instance()->disableKillSwitch());
}

bool IpcServer::enablePeerTraffic(const QJsonObject &configStr)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::enablePeerTraffic";
#endif

    return logResult("killswitch enablePeerTraffic", KillSwitch::instance()->enablePeerTraffic(configStr));
}

bool IpcServer::refreshKillSwitch(bool enabled)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::refreshKillSwitch";
#endif

    return logResult(QString("killswitch refresh, strict mode %1").arg(enabled ? "on" : "off"), KillSwitch::instance()->refresh(enabled));
}

bool IpcServer::xrayStart(const QString& cfg)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::xrayStart";
#endif

    return logResult("xrayStart", Xray::getInstance().start(cfg) != 0);
}

bool IpcServer::xrayStop()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::xrayStop";
#endif

    return logResult("xrayStop", Xray::getInstance().stopAny());
}

qint64 IpcServer::xrayStartOwned(const QString& cfg)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::xrayStartOwned";
#endif

    return logResult("xrayStartOwned", Xray::getInstance().start(cfg));
}

bool IpcServer::xrayStopOwned(qint64 token)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::xrayStopOwned";
#endif

    return logResult(QString("xrayStopOwned, token %1").arg(token), Xray::getInstance().stop(token));
}

qint64 IpcServer::xrayCurrentToken()
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::xrayCurrentToken";
#endif

    return Xray::getInstance().currentToken();
}

bool IpcServer::isTcpPortAvailable(int port)
{
#ifdef MZ_DEBUG
    qDebug() << "IpcServer::isTcpPortAvailable" << port;
#endif

    if (port < 1 || port > 65535) {
        return false;
    }

    QTcpServer server;
    const bool available = server.listen(QHostAddress::LocalHost, static_cast<quint16>(port));
    server.close();
    return available;
}
