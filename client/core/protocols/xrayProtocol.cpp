#include "xrayProtocol.h"

#include "core/models/protocols/xrayProtocolConfig.h"
#include "core/protocols/protocolUtils.h"
#include "core/protocols/xrayRouting.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/constants/protocolConstants.h"
#include "core/utils/ipcClient.h"
#include "core/utils/networkUtilities.h"
#include "core/utils/serialization/serialization.h"
#include "ipc.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QtCore/qlogging.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qprocess.h>

#include <exception>

#ifdef Q_OS_WIN
    #include <winsock2.h>
    #include <ws2ipdef.h>
    #include <iphlpapi.h>
#endif

#ifdef Q_OS_MACOS
static const QString tunName = "utun22";
#else
static const QString tunName = "tun2";
#endif

namespace
{
#ifdef Q_OS_WIN
constexpr int trafficPollIntervalMs = 10000;

bool readTunCounters(quint64 &receivedBytes, quint64 &sentBytes)
{
    MIB_IF_ROW2 row {};
    if (ConvertInterfaceAliasToLuid(reinterpret_cast<const wchar_t *>(tunName.utf16()), &row.InterfaceLuid) != NO_ERROR
        || GetIfEntry2(&row) != NO_ERROR) {
        return false;
    }
    receivedBytes = row.InOctets;
    sentBytes = row.OutOctets;
    return true;
}
#endif

QJsonObject withXrayResolver(QJsonObject streamSettings)
{
    QJsonObject sockopt = streamSettings.value("sockopt").toObject();
    if (!sockopt.contains("domainStrategy")) {
        sockopt.insert("domainStrategy", "UseIP");
        streamSettings.insert("sockopt", sockopt);
    }
    return streamSettings;
}

template<typename PinHost>
QJsonObject withPinnedDownloadSettings(QJsonObject xhttpSettings, const PinHost &pinHost)
{
    const QJsonObject downloadSettings = xhttpSettings.value("downloadSettings").toObject();
    if (downloadSettings.isEmpty()) {
        return xhttpSettings;
    }

    pinHost(downloadSettings.value("address").toString());
    xhttpSettings.insert("downloadSettings", withXrayResolver(downloadSettings));
    return xhttpSettings;
}

// Once the tunnel is up the system resolver points into it, so xray must not resolve its own server through it
void pinServerAddress(QJsonObject &xrayConfig, const QString &hostName, const QString &address)
{
    QJsonObject dns = xrayConfig.value("dns").toObject();
    QJsonObject hosts = dns.value("hosts").toObject();
    const auto pinHost = [&](const QString &host) {
        if (host.isEmpty() || hosts.contains(host) || !QHostAddress(host).isNull()) {
            return;
        }
        const QString ip = host == hostName ? address : NetworkUtilities::getIPAddress(host);
        if (!ip.isEmpty()) {
            hosts.insert(host, ip);
        }
    };
    pinHost(hostName);

    QJsonArray outbounds = xrayConfig.value("outbounds").toArray();
    for (int i = 0; i < outbounds.size(); ++i) {
        QJsonObject outbound = outbounds.at(i).toObject();
        const QJsonObject settings = outbound.value("settings").toObject();
        if (!settings.contains("vnext") && !settings.contains("servers")) {
            continue;
        }

        QJsonObject streamSettings = withXrayResolver(outbound.value("streamSettings").toObject());
        if (streamSettings.contains("xhttpSettings")) {
            QJsonObject xhttpSettings = withPinnedDownloadSettings(streamSettings.value("xhttpSettings").toObject(), pinHost);
            if (xhttpSettings.contains("extra")) {
                xhttpSettings.insert("extra", withPinnedDownloadSettings(xhttpSettings.value("extra").toObject(), pinHost));
            }
            streamSettings.insert("xhttpSettings", xhttpSettings);
        }
        outbound.insert("streamSettings", streamSettings);
        outbounds[i] = outbound;
    }
    xrayConfig.insert("outbounds", outbounds);

    if (!hosts.isEmpty()) {
        dns.insert("hosts", hosts);
        xrayConfig.insert("dns", dns);
    }
}
} // namespace

XrayProtocol::XrayProtocol(const QJsonObject &configuration, QObject *parent) : VpnProtocol(configuration, parent)
{
    m_vpnGateway = amnezia::protocols::xray::defaultLocalAddr;
    m_vpnLocalAddress = amnezia::protocols::xray::defaultLocalAddr;
    m_routeGateway = NetworkUtilities::getGatewayAndIface().first;

    m_routeMode = static_cast<amnezia::RouteMode>(configuration.value(amnezia::configKey::splitTunnelType).toInt());
    m_remoteAddress = NetworkUtilities::getIPAddress(m_rawConfig.value(amnezia::configKey::hostName).toString());

    const QString primaryDns = configuration.value(amnezia::configKey::dns1).toString();
    m_dnsServers.push_back(QHostAddress(primaryDns));
    if (primaryDns != amnezia::protocols::dns::amneziaDnsIp) {
        const QString secondaryDns = configuration.value(amnezia::configKey::dns2).toString();
        m_dnsServers.push_back(QHostAddress(secondaryDns));
    }

    QJsonObject xrayConfiguration = configuration.value(ProtocolUtils::key_proto_config_data(Proto::Xray)).toObject();
    if (xrayConfiguration.isEmpty()) {
        xrayConfiguration = configuration.value(ProtocolUtils::key_proto_config_data(Proto::SSXray)).toObject();
    }

    if (xrayConfiguration.isEmpty()) {
        qWarning() << "Xray config wrapper is empty";
        m_xrayConfig = {};
    }

    m_xrayConfig = QJsonDocument::fromJson(xrayConfiguration.value(amnezia::configKey::config).toString().toUtf8()).object();
    if (m_xrayConfig.isEmpty()) {
        qWarning() << "Xray config string is not a valid JSON object";
        m_xrayConfig = {};
    }

#ifdef Q_OS_WIN
    m_trafficTimer.setInterval(trafficPollIntervalMs);
    connect(&m_trafficTimer, &QTimer::timeout, this, [this]() {
        quint64 receivedBytes = 0;
        quint64 sentBytes = 0;
        if (readTunCounters(receivedBytes, sentBytes)) {
            setBytesChanged(receivedBytes, sentBytes);
        }
    });
#endif
}

XrayProtocol::~XrayProtocol()
{
    qDebug() << "XrayProtocol::~XrayProtocol()";
    XrayProtocol::stop();
}

ErrorCode XrayProtocol::start()
{
    resetStopGuard();

    qDebug() << "XrayProtocol::start()";

    // Inject SOCKS5 auth into the inbound before starting xray.
    // Re-uses existing credentials if the config already has them (e.g. imported config).
    amnezia::serialization::inbounds::InboundCredentials creds;
    try {
        creds = amnezia::serialization::inbounds::EnsureInboundAuth(m_xrayConfig);
    } catch (const std::exception &e) {
        qCritical() << "EnsureInboundAuth failed:" << e.what();
        return ErrorCode::InternalError;
    }
    m_socksUser = creds.username;
    m_socksPassword = creds.password;
    m_socksPort = creds.port;

    const QString hostName = m_rawConfig.value(amnezia::configKey::hostName).toString();
    if (m_remoteAddress.isEmpty()) {
        m_remoteAddress = NetworkUtilities::getIPAddress(hostName);
    }
    if (m_remoteAddress.isEmpty()) {
        qCritical() << "[xray] the server address could not be resolved";
        return ErrorCode::InternalError;
    }

    QJsonObject xrayConfig = m_xrayConfig;
    pinServerAddress(xrayConfig, hostName, m_remoteAddress);

    if (m_routeMode == amnezia::RouteMode::VpnOnlyForwardSites) {
        amnezia::xrayRouting::routeOnlySitesThroughProxy(xrayConfig, m_rawConfig.value(amnezia::configKey::splitTunnelDomains).toArray(),
                                                         m_rawConfig.value(amnezia::configKey::splitTunnelSites).toArray());
    }

    QStringList dnsServers;
    for (const QHostAddress &dnsServer : std::as_const(m_dnsServers)) {
        dnsServers.append(dnsServer.toString());
    }
    amnezia::xrayRouting::resolveDnsInsideCore(xrayConfig, dnsServers);

    xrayConfig.insert(amnezia::protocols::xray::logBlock,
                      QJsonObject { { amnezia::protocols::xray::logLevel, "warning" }, { "access", "none" } });
    qInfo() << "[xray] starting the core, log level warning";

    QString xrayConfigStr = QJsonDocument(xrayConfig).toJson(QJsonDocument::Compact);
    if (xrayConfigStr.isEmpty()) {
        qCritical() << "Xray config is empty";
        return ErrorCode::XrayExecutableCrashed;
    }

    xrayConfigStr = amnezia::sanitizeNativeConfig(xrayConfigStr);

    return IpcClient::withInterface(
            [&](QSharedPointer<IpcInterfaceReplica> iface) {
                auto xrayStart = iface->xrayStartOwned(xrayConfigStr);
                if (!xrayStart.waitForFinished() || xrayStart.returnValue() == 0) {
                    qCritical() << "Failed to start xray";
                    return ErrorCode::XrayExecutableCrashed;
                }
                m_xrayToken = xrayStart.returnValue();
                return startTun2Socks();
            },
            []() { return ErrorCode::AmneziaServiceConnectionFailed; });
}

void XrayProtocol::stop()
{
    if (guardStop()) {
        return;
    }

    qDebug() << "XrayProtocol::stop()";

#ifdef Q_OS_WIN
    m_trafficTimer.stop();
#endif

    IpcClient::withInterface([this](QSharedPointer<IpcInterfaceReplica> iface) {
        auto StartRoutingIpv6 = iface->StartRoutingIpv6();
        if (!StartRoutingIpv6.waitForFinished() || !StartRoutingIpv6.returnValue())
            qWarning() << "Failed to start routing ipv6";

        auto restoreResolvers = iface->restoreResolvers();
        if (!restoreResolvers.waitForFinished() || !restoreResolvers.returnValue())
            qWarning() << "Failed to restore resolvers";

        auto deleteTun = iface->deleteTun(tunName);
        if (!deleteTun.waitForFinished() || !deleteTun.returnValue())
            qWarning() << "Failed to delete tun";

        auto xrayStop = iface->xrayStopOwned(m_xrayToken);
        if (!xrayStop.waitForFinished() || !xrayStop.returnValue())
            qWarning() << "Failed to stop xray";
        m_xrayToken = 0;
    });

    if (m_tun2socksProcess) {
        m_tun2socksProcess->blockSignals(true);

#ifndef Q_OS_WIN
        m_tun2socksProcess->terminate();
        auto waitForFinished = m_tun2socksProcess->waitForFinished(1000);
        if (!waitForFinished.waitForFinished() || !waitForFinished.returnValue()) {
            qWarning() << "Failed to terminate tun2socks. Killing the process...";
            m_tun2socksProcess->kill();
        }
#else
        // terminate does not do anything useful on Windows
        // so just kill the process
        m_tun2socksProcess->kill();
#endif

        m_tun2socksProcess->close();
        m_tun2socksProcess.reset();
    }

    setConnectionState(Vpn::ConnectionState::Disconnected);
}

ErrorCode XrayProtocol::startTun2Socks()
{
    m_tun2socksProcess = IpcClient::CreatePrivilegedProcess();
    m_tun2socksOutputTail.clear();
    if (!m_tun2socksProcess->waitForSource()) {
        return ErrorCode::AmneziaServiceConnectionFailed;
    }

    const QString proxyUrl = QString("socks5://%1:%2@127.0.0.1:%3").arg(m_socksUser, m_socksPassword, QString::number(m_socksPort));

    m_tun2socksProcess->setProgram(PermittedProcess::Tun2Socks);
    m_tun2socksProcess->setArguments({ "-device", QString("tun://%1").arg(tunName), "-proxy", proxyUrl });

    connect(
            m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::readyReadStandardError, this, 
            [this]() {
                if (!m_tun2socksProcess) {
                    return;
                }

                auto readAllStandardError = m_tun2socksProcess->readAllStandardError();
                if (!readAllStandardError.waitForFinished()) {
                    qWarning() << "Failed to read output from tun2socks";
                    return;
                }

                const QString output = readAllStandardError.returnValue();
                appendTun2SocksOutput(output);

                for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
                    if (!line.contains("[TCP]") && !line.contains("[UDP]")) {
                        qDebug() << "[tun2socks]:" << line;
                    }
                }

                if (output.contains("[STACK] tun://") && output.contains("<-> socks5://")) {
                    disconnect(m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::readyReadStandardOutput, this, nullptr);

                    if (ErrorCode res = setupRouting(); res != ErrorCode::NoError) {
                        qCritical() << "[xray] routing setup failed, stopping";
                        stop();
                        setLastError(res);
                    } else {
                        qInfo() << "[xray] tun2socks is up and routing is set";
                        setConnectionState(Vpn::ConnectionState::Connected);
#ifdef Q_OS_WIN
                        m_trafficTimer.start();
#endif
                    }
                }
            },
            Qt::QueuedConnection);

    connect(
            m_tun2socksProcess.data(), &IpcProcessInterfaceReplica::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                // Check stdout for "resource busy" — the TUN device was not yet released
                // by the previous tun2socks instance. Retry after a short delay.
                bool resourceBusy = false;
                if (m_tun2socksProcess) {
                    auto readOut = m_tun2socksProcess->readAllStandardOutput();
                    if (readOut.waitForFinished()) {
                        const QString output = readOut.returnValue();
                        resourceBusy = output.contains("resource busy");
                        appendTun2SocksOutput(output);
                    }
                }

                if (resourceBusy && m_tun2socksRetryCount < maxTun2SocksRetries) {
                    m_tun2socksRetryCount++;
                    qWarning() << QString("Tun2socks: TUN resource busy, retrying (%1/%2) in %3ms...")
                                      .arg(m_tun2socksRetryCount)
                                      .arg(maxTun2SocksRetries)
                                      .arg(tun2socksRetryDelayMs);
                    QTimer::singleShot(tun2socksRetryDelayMs, this, [this]() {
                        if (ErrorCode err = startTun2Socks(); err != ErrorCode::NoError) {
                            stop();
                            setLastError(err);
                        }
                    });
                    return;
                }

                m_tun2socksRetryCount = 0;

                qCritical().noquote() << QString("[tun2socks] process %1 with exit code %2, last output:\n%3")
                                                 .arg(exitStatus == QProcess::ExitStatus::CrashExit ? "crashed" : "exited")
                                                 .arg(exitCode)
                                                 .arg(m_tun2socksOutputTail.join('\n'));
                stop();
                setLastError(ErrorCode::Tun2SockExecutableCrashed);
            },
            Qt::QueuedConnection);

    m_tun2socksProcess->start();
    return ErrorCode::NoError;
}

void XrayProtocol::appendTun2SocksOutput(const QString &output)
{
    m_tun2socksOutputTail += output.split('\n', Qt::SkipEmptyParts);
    if (m_tun2socksOutputTail.size() > tun2socksOutputTailLines) {
        m_tun2socksOutputTail = m_tun2socksOutputTail.mid(m_tun2socksOutputTail.size() - tun2socksOutputTailLines);
    }
}

ErrorCode XrayProtocol::setupRouting()
{
    return IpcClient::withInterface(
            [this](QSharedPointer<IpcInterfaceReplica> iface) -> ErrorCode {
#ifdef Q_OS_WIN
                const int inetAdapterIndex = NetworkUtilities::AdapterIndexTo(QHostAddress(m_remoteAddress));
#endif
                auto createTun = iface->createTun(tunName, amnezia::protocols::xray::defaultLocalAddr);
                if (!createTun.waitForFinished() || !createTun.returnValue()) {
                    qCritical() << "Failed to assign IP address for TUN";
                    return ErrorCode::InternalError;
                }

                auto updateResolvers = iface->updateResolvers(tunName, m_dnsServers);
                if (!updateResolvers.waitForFinished() || !updateResolvers.returnValue()) {
                    qCritical() << "Failed to set DNS resolvers for TUN";
                    return ErrorCode::InternalError;
                }

#ifdef Q_OS_WIN
                int vpnAdapterIndex = -1;
                QList<QNetworkInterface> netInterfaces = QNetworkInterface::allInterfaces();
                for (auto &netInterface : netInterfaces) {
                    for (auto &address : netInterface.addressEntries()) {
                        if (m_vpnLocalAddress == address.ip().toString())
                            vpnAdapterIndex = netInterface.index();
                    }
                }
#else
                static const int vpnAdapterIndex = 0;
#endif
                const bool killSwitchEnabled = QVariant(m_rawConfig.value(configKey::killSwitchOption).toString()).toBool();
                if (killSwitchEnabled) {
                    if (vpnAdapterIndex == -1) {
                        qCritical() << "Failed to get vpnAdapterIndex for killswitch";
                        return ErrorCode::InternalError;
                    }

                    QJsonObject config = m_rawConfig;
                    config.insert("vpnServer", m_remoteAddress);

                    auto enableKillSwitch = IpcClient::Interface()->enableKillSwitch(config, vpnAdapterIndex);
                    if (!enableKillSwitch.waitForFinished() || !enableKillSwitch.returnValue()) {
                        qCritical() << "Failed to enable killswitch";
                        return ErrorCode::InternalError;
                    }
                }

                if (m_routeMode == amnezia::RouteMode::VpnAllSites || m_routeMode == amnezia::RouteMode::VpnOnlyForwardSites) {
                    if (!m_routeGateway.isEmpty() && QHostAddress(m_remoteAddress).protocol() == QAbstractSocket::IPv4Protocol) {
                        auto routeServer = iface->routeAddList(m_routeGateway, QStringList() << m_remoteAddress);
                        if (!routeServer.waitForFinished() || routeServer.returnValue() != 1) {
                            qWarning() << "[xray] failed to keep the server route on the physical gateway";
                        }
                    }

                    static const QStringList subnets = { "1.0.0.0/8",  "2.0.0.0/7",  "4.0.0.0/6",  "8.0.0.0/5",
                                                         "16.0.0.0/4", "32.0.0.0/3", "64.0.0.0/2", "128.0.0.0/1" };

                    auto routeAddList = iface->routeAddList(m_vpnGateway, subnets);
                    if (!routeAddList.waitForFinished() || routeAddList.returnValue() != subnets.count()) {
                        qCritical() << "Failed to set routes for TUN";
                        return ErrorCode::InternalError;
                    }
                }

                auto StopRoutingIpv6 = iface->StopRoutingIpv6();
                if (!StopRoutingIpv6.waitForFinished() || !StopRoutingIpv6.returnValue()) {
                    qCritical() << "Failed to disable IPv6 routing";
                    return ErrorCode::InternalError;
                }

#ifdef Q_OS_WIN
                if (inetAdapterIndex != -1 && vpnAdapterIndex != -1) {
                    QJsonObject config = m_rawConfig;
                    config.insert("inetAdapterIndex", inetAdapterIndex);
                    config.insert("vpnAdapterIndex", vpnAdapterIndex);
                    config.insert("vpnGateway", m_vpnGateway);
                    config.insert("vpnServer", m_remoteAddress);

                    auto enablePeerTraffic = iface->enablePeerTraffic(config);
                    if (!enablePeerTraffic.waitForFinished() || !enablePeerTraffic.returnValue()) {
                        qCritical() << "Failed to enable peer traffic";
                        return ErrorCode::InternalError;
                    }
                } else if (killSwitchEnabled) {
                    qCritical() << "Failed to get adapter indexes for killswitch";
                    return ErrorCode::InternalError;
                } else {
                    qWarning() << "Failed to get adapter indexes. Split-tunneling disabled";
                }
#endif
                return ErrorCode::NoError;
            },
            []() { return ErrorCode::AmneziaServiceConnectionFailed; });
}
