#include "router_win.h"

#include <string>
#include <tchar.h>

#include <QProcess>
#include <QtConcurrent>

#include <core/utils/networkUtilities.h>

QList<QString> RouterWin::kIpv6Subnets = { "fc00::/7", "2000::/4", "3000::/4" };

RouterWin &RouterWin::Instance()
{
    static RouterWin s;
    return s;
}

int RouterWin::routeAddList(const QString &gw, const QStringList &ips)
{
//    qDebug().noquote() << QString("ROUTE ADD List: IPs size:%1, GW: %2")
//                          .arg(ips.size())
//                          .arg(gw);

//    qDebug().noquote() << QString("ROUTE ADD List: IPs:\n%1")
//                          .arg(ips.join("\n"));


    if (!NetworkUtilities::checkIPv4Format(gw)) {
        qCritical().noquote() << "Trying to add invalid route, gw: " << gw;
        return 0;
    }

    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    BOOL bOrder = FALSE;
    DWORD dwStatus = 0;


    // Find out how big our buffer needs to be.
    dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    if (dwStatus == ERROR_INSUFFICIENT_BUFFER) {
        // Allocate the memory for the table
        if (!(pIpForwardTable = (PMIB_IPFORWARDTABLE) malloc(dwSize))) {
            qDebug() << "Malloc failed. Out of memory.";
            return 0;
        }
        // Now get the table.
        dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    }

    if (dwStatus != ERROR_SUCCESS) {
        qDebug() << "getIpForwardTable failed.";
        if (pIpForwardTable)
            free(pIpForwardTable);
        return 0;
    }

    int success_count = 0;
    MIB_IPFORWARDROW ipfrow;

    ipfrow.dwForwardPolicy = 0;
    ipfrow.dwForwardAge = INFINITE;

    ipfrow.dwForwardNextHop = inet_addr(gw.toStdString().c_str());
    ipfrow.dwForwardType = MIB_IPROUTE_TYPE_INDIRECT;	/* XXX - next hop != final dest */
    ipfrow.dwForwardProto = MIB_IPPROTO_NETMGMT;	/* XXX - MIB_PROTO_NETMGMT */

    // Set iface for route
    IPAddr dwGwAddr = inet_addr(gw.toStdString().c_str());
    if (GetBestInterface(dwGwAddr, &ipfrow.dwForwardIfIndex) != NO_ERROR) {
        qDebug() << "Router::routeAddList : GetBestInterface failed";
        return false;
    }

    // Get TAP iface metric to set it for new routes
    MIB_IPINTERFACE_ROW tap_iface;
    InitializeIpInterfaceEntry(&tap_iface);
    tap_iface.InterfaceIndex = ipfrow.dwForwardIfIndex;
    tap_iface.Family = AF_INET;
    dwStatus  = GetIpInterfaceEntry(&tap_iface);
    if (dwStatus == NO_ERROR){
        ipfrow.dwForwardMetric1 = tap_iface.Metric + 1;
    }
    else {
        qDebug() << "Router::routeAddList: failed GetIpInterfaceEntry(), Error:" << dwStatus;
        ipfrow.dwForwardMetric1 = 256;
    }
    ipfrow.dwForwardMetric2 = 0;
    ipfrow.dwForwardMetric3 = 0;
    ipfrow.dwForwardMetric4 = 0;
    ipfrow.dwForwardMetric5 = 0;

    for (int i = 0; i < ips.size(); ++i) {
        QString ipWithMask = ips.at(i);
        QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipWithMask);

        if (!NetworkUtilities::checkIPv4Format(ip)) {
            qCritical().noquote() << "Critical, trying to add invalid route, ip: " << ip;
            continue;
        }

        QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipWithMask);

        // address
        ipfrow.dwForwardDest = inet_addr(ip.toStdString().c_str());

        // mask
        in_addr maskAddr;
        inet_pton(AF_INET, mask.toStdString().c_str(), &maskAddr);
        ipfrow.dwForwardMask = maskAddr.S_un.S_addr;

        dwStatus = CreateIpForwardEntry(&ipfrow);
        if (dwStatus == NO_ERROR){
            m_ipForwardRows.insert(ip, ipfrow);
            success_count++;
        }
        else if (dwStatus == ERROR_OBJECT_ALREADY_EXISTS) {
            m_ipForwardRows.insert(ip, ipfrow);
            success_count++;
            qDebug() << "Router::routeAdd: warning, route already exist:" << ip << gw;
        }
        else {
            qDebug() << "Router::routeAdd: failed CreateIpForwardEntry(), Error:" << ip << gw << dwStatus;
        }
    }

    // Free resources
    if (pIpForwardTable)
        free(pIpForwardTable);

    qDebug() << "Router::routeAddList finished, success: " << success_count << "/" << ips.size();

    return success_count;
}

bool RouterWin::clearSavedRoutes()
{
    if (m_ipForwardRows.isEmpty()) return true;

    qDebug() << "RouterWin::clearSavedRoutes forward rows size:" << m_ipForwardRows.size();

    // Declare and initialize variables
    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    BOOL bOrder = FALSE;
    DWORD dwStatus = 0;

    // Find out how big our buffer needs to be.
    dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    if (dwStatus == ERROR_INSUFFICIENT_BUFFER) {
        // Allocate the memory for the table
        if (!(pIpForwardTable = (PMIB_IPFORWARDTABLE) malloc(dwSize))) {
            qDebug() << "Router::clearSavedRoutes : Malloc failed. Out of memory";
            return false;
        }
        // Now get the table.
        dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    }

    if (dwStatus != ERROR_SUCCESS) {
        qDebug() << "Router::clearSavedRoutes : getIpForwardTable failed";
        if (pIpForwardTable)
            free(pIpForwardTable);

        return false;
    }

    int removed_count = 0;
    for (auto i = m_ipForwardRows.begin(); i != m_ipForwardRows.end(); ++i) {
        dwStatus = DeleteIpForwardEntry(&i.value());

        if (dwStatus != ERROR_SUCCESS) {
            qDebug() << "Router::clearSavedRoutes : Could not delete old row" << i.key();
        }
        else  removed_count++;
    }

    if (pIpForwardTable)
        free(pIpForwardTable);

    qDebug() << "Router::clearSavedRoutes : removed routes:" << removed_count << "of" << m_ipForwardRows.size();
    m_ipForwardRows.clear();

    return true;
}

int RouterWin::routeDeleteList(const QString &gw, const QStringList &ips)
{
//    qDebug().noquote() << QString("ROUTE DELETE List: IPs size:%1, GW: %2")
//                          .arg(ips.size())
//                          .arg(gw);

//    qDebug().noquote() << QString("ROUTE DELETE List: IPs:\n%1")
//                          .arg(ips.join("\n"));

    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    BOOL bOrder = FALSE;
    DWORD dwStatus = 0;
    ULONG gw_addr = inet_addr(gw.toStdString().c_str());

    // Find out how big our buffer needs to be.
    dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    if (dwStatus == ERROR_INSUFFICIENT_BUFFER) {
        // Allocate the memory for the table
        if (!(pIpForwardTable = (PMIB_IPFORWARDTABLE) malloc(dwSize))) {
            qDebug() << "Malloc failed. Out of memory.";
            return 0;
        }
        // Now get the table.
        dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    }

    if (dwStatus != ERROR_SUCCESS) {
        qDebug() << "getIpForwardTable failed.";
        if (pIpForwardTable)
            free(pIpForwardTable);
        return 0;
    }

    int success_count = 0;

    QMap<ULONG, QPair<QString,ULONG>> ipMap;
    for (int i = 0; i < ips.size(); ++i) {
        QString ipMask = ips.at(i);
        if (ipMask.isEmpty()) continue;
        QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipMask);
        QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipMask);

        if (ip.isEmpty()) continue;

        in_addr maskAddr;
        inet_pton(AF_INET, mask.toStdString().c_str(), &maskAddr);

        ipMap.insert(inet_addr(ip.toStdString().c_str()), qMakePair(ipMask, maskAddr.S_un.S_addr));
    }

    for (int i = 0; i < (int) pIpForwardTable->dwNumEntries; i++) {
        MIB_IPFORWARDROW ipfrow = pIpForwardTable->table[i];

        if(ipMap.contains(ipfrow.dwForwardDest) &&
                ipMap.value(ipfrow.dwForwardDest).second == ipfrow.dwForwardMask &&
                ipfrow.dwForwardNextHop == gw_addr) {
            dwStatus = DeleteIpForwardEntry(&pIpForwardTable->table[i]);
            if (dwStatus == ERROR_SUCCESS) {
                m_ipForwardRows.remove(ipMap.value(ipfrow.dwForwardDest).first);
                success_count++;
            }
        }
    }

    // Free resources
    if (pIpForwardTable)
        free(pIpForwardTable);

    qDebug() << "Router::routeDeleteList finished, success: " << success_count << "/" << ips.size();

    return success_count;
}

bool RouterWin::flushDns()
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    QString command = QString("ipconfig /flushdns");

    p.start(command);
    p.waitForFinished();
    return true;
    //qDebug().noquote() << "OUTPUT ipconfig /flushdns: " + p.readAll();
}

void RouterWin::resetIpStack()
{
//    {
//        QProcess p;
//        QString command = QString("ipconfig /release");
//        p.start(command);
//    }
    {
        QProcess p;
        QString command = QString("netsh int ip reset");
        p.start(command);
        p.waitForFinished();
    }
    {
        QProcess p;
        QString command = QString("netsh winsock reset");
        p.start(command);
        p.waitForFinished();
    }
}

bool RouterWin::createTun(const QString &dev, const QString &subnet)
{
    NET_LUID luid;
    DWORD res = ConvertInterfaceAliasToLuid(reinterpret_cast<const wchar_t*>(dev.utf16()), &luid);
    if (res != NO_ERROR) {
        qCritical() << "Failed to convert luid: " << res;
        return false;
    }

    HANDLE hEvent = CreateEvent(nullptr, true, false, nullptr);
    if (!hEvent) {
        qCritical() << "Failed to allocate event object";
        return false;
    }
    auto _guardEvent = qScopeGuard([hEvent](){ CloseHandle(hEvent); });

    struct {
        HANDLE hEvent;
        NET_LUID luid;
        const QString &subnet;
        bool found;
    } ctx = { .hEvent = hEvent, .luid = luid, .subnet = subnet, .found = false };

    auto cb = [](void *priv, MIB_UNICASTIPADDRESS_ROW *row, MIB_NOTIFICATION_TYPE NotificationType) {
        auto* c = reinterpret_cast<decltype(ctx)*>(priv);
        if (row != nullptr && row->InterfaceLuid.Value == c->luid.Value && row->Address.si_family == AF_INET) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(row->Address.Ipv4.sin_family, &row->Address.Ipv4.sin_addr, ip, INET_ADDRSTRLEN);
            if (c->subnet == ip) {
                c->found = true;
                SetEvent(c->hEvent);
            }
        }
    };

    HANDLE hNotif;
    res = NotifyUnicastIpAddressChange(AF_INET, cb, &ctx, false, &hNotif);
    if (res != NO_ERROR) {
        qCritical() << "Failed to subscribe to interface change";
        return false;
    }
    auto _guardNotif = qScopeGuard([hNotif](){ CancelMibChangeNotify2(hNotif); });

    MIB_UNICASTIPADDRESS_ROW row;
    InitializeUnicastIpAddressEntry(&row);

    row.InterfaceLuid = luid;
    row.Address.si_family = AF_INET;

    inet_pton(AF_INET, subnet.toStdString().c_str(), &row.Address.Ipv4.sin_addr);

    row.OnLinkPrefixLength = 32;
    row.ValidLifetime = 0xffffffff;
    row.PreferredLifetime = 0xffffffff;
    row.DadState = IpDadStatePreferred;

    res = CreateUnicastIpAddressEntry(&row);
    if (res != NO_ERROR && res != ERROR_OBJECT_ALREADY_EXISTS) {
        qDebug() << "Failed to create IP address:" << res;
        return false;
    }

    res = WaitForSingleObject(hEvent, 10000);
    if (res == WAIT_TIMEOUT) {
        qCritical() << "Timeout of waiting for IP assignment for " << dev << " device";
        return false;
    }

    return ctx.found;
}

bool RouterWin::updateResolvers(const QString& ifname, const QList<QHostAddress>& resolvers)
{
    return m_dnsUtil->updateResolvers(ifname, resolvers);
}

bool RouterWin::restoreResolvers() {
    return m_dnsUtil->restoreResolvers();
}

QNetworkInterface RouterWin::findLoopbackIface()
{
    for (auto iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) {
            return iface;
        }
    }
    return {};
}

bool RouterWin::StopRoutingIpv6()
{
    qDebug() << "RouterWin::StopRoutingIpv6";

    if (auto loopback = findLoopbackIface(); loopback.isValid()) {
        QFuture<bool> res = QtConcurrent::mappedReduced(kIpv6Subnets, [loopback](const QString &subnet) -> bool {
            int res = QProcess::execute("netsh", { "interface", "ipv6", "add", "route", subnet, QString("interface=%1").arg(loopback.index()), "metric=0", "store=active" });
            return res == 0;
        },
        [](bool &result, bool success) {
            result = result && success;
        }, true);

        res.waitForFinished();
        return res.result();
    }

    return false;
}

bool RouterWin::StartRoutingIpv6()
{
    qDebug() << "RouterWin::StartRoutingIpv6";

    if (auto loopback = findLoopbackIface(); loopback.isValid()) {
        QFuture<bool> res = QtConcurrent::mappedReduced(kIpv6Subnets, [loopback](const QString &subnet) -> bool {
            int res = QProcess::execute("netsh", { "interface", "ipv6", "delete", "route", subnet, QString("interface=%1").arg(loopback.index()) });
            return res == 0;
        },
        [](bool &result, bool success) {
            result = result && success;
        }, true);
    }

    return false;
}
