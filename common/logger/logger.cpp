#include "logger.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QStandardPaths>

#include "core/utils/utilities.h"
#include "version.h"

#ifdef AMNEZIA_DESKTOP
    #include <core/utils/ipcClient.h>
#endif

QFile Logger::m_file;
QTextStream Logger::m_textStream;
QString Logger::m_logFileName = QString("%1.log").arg(APPLICATION_NAME);
QString Logger::m_serviceLogFileName = QString("%1.log").arg(SERVICE_NAME);

namespace
{
    bool isPublicIpv4(const QStringList &octets)
    {
        const int a = octets.at(0).toInt();
        const int b = octets.at(1).toInt();
        if (a == 0 || a == 10 || a == 127 || a >= 224) {
            return false;
        }
        if ((a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127)) {
            return false;
        }
        return true;
    }

    QString maskDomain(const QString &domain)
    {
        const QString tld = domain.section('.', -1);
        return domain.left(1) + "***." + tld;
    }

    QString maskHost(const QString &host)
    {
        static const QRegularExpression ipv4(QStringLiteral(R"(^\d{1,3}(\.\d{1,3}){3}$)"));
        if (ipv4.match(host).hasMatch()) {
            const QStringList octets = host.split('.');
            return isPublicIpv4(octets) ? QString("%1.*.*.%2").arg(octets.first(), octets.last()) : host;
        }
        return host.contains('.') ? maskDomain(host) : host;
    }

    template<typename Replacer>
    QString replaceAll(const QString &input, const QRegularExpression &re, Replacer replacer)
    {
        QString result;
        qsizetype last = 0;
        auto it = re.globalMatch(input);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            result += input.mid(last, match.capturedStart() - last);
            result += replacer(match);
            last = match.capturedEnd();
        }
        if (last == 0) {
            return input;
        }
        return result + input.mid(last);
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (msg.simplified().isEmpty()) {
        return;
    }

    // Skip annoying messages from Qt
    if (msg.contains("OpenType support missing for")) {
        return;
    }

    if (msg.startsWith("Unknown property") || msg.startsWith("Could not create pixmap")
        || msg.startsWith("Populating font") || msg.startsWith("stale focus object")) {
        return;
    }

    switch (type) {
    case QtDebugMsg: Logger::Instance().debug() << msg; break;
    case QtInfoMsg: Logger::Instance().info() << msg; break;
    case QtWarningMsg: Logger::Instance().warning() << msg; break;
    case QtCriticalMsg: Logger::Instance().error() << msg; break;
    case QtFatalMsg: {
        Logger::Instance().error() << msg;
    } // Brackets are needed to ensure the destructor of LogStreamer is called before abort()
        abort();
    }
}

QString Logger::maskSensitive(const QString &message)
{
    static const QRegularExpression shareLink(
            QStringLiteral(R"(\b(vless|vmess|trojan|ssr?|hysteria2?|hy2|tuic|vpn|wireguard|awg)://[^\s"'<>]+)"),
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression url(
            QStringLiteral(R"(\b(https?|socks5h?|socks)://([^\s/@"'<>]+@)?([^\s/?#:"'<>\[\]]+)(:\d+)?((?:[/?#][^\s"'<>]*)?))"),
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression uuid(
            QStringLiteral(R"(\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b)"));
    static const QRegularExpression base64Key(QStringLiteral(R"((?<![A-Za-z0-9+/])[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]=(?![A-Za-z0-9+/=]))"));
    static const QRegularExpression domainWithPort(
            QStringLiteral(R"(\b((?:[A-Za-z0-9-]+\.)+(?!(?:qml|cpp|h|hpp|c|mm|js|go|kt|java|swift|py)\b)[A-Za-z]{2,63}):(\d{1,5})\b)"));
    static const QRegularExpression lookupDomain(QStringLiteral(R"(\b(lookup|resolve[ds]?) ((?:[A-Za-z0-9-]+\.)+[A-Za-z]{2,63})\b)"));
    static const QRegularExpression ipv4(QStringLiteral(R"((?<![\d.])(\d{1,3}(?:\.\d{1,3}){3})(/\d{1,2})?(?!\d|\.\d))"));

    QString result = replaceAll(message, shareLink, [](const QRegularExpressionMatch &m) { return m.captured(1) + "://***"; });
    result = replaceAll(result, url, [](const QRegularExpressionMatch &m) {
        return m.captured(1) + "://" + (m.captured(2).isEmpty() ? QString() : QStringLiteral("***@")) + maskHost(m.captured(3))
                + m.captured(4) + (m.captured(5).isEmpty() ? QString() : QStringLiteral("/***"));
    });
    result = replaceAll(result, uuid, [](const QRegularExpressionMatch &) { return QStringLiteral("<uuid>"); });
    result = replaceAll(result, base64Key, [](const QRegularExpressionMatch &) { return QStringLiteral("<key>"); });
    result = replaceAll(result, domainWithPort,
                        [](const QRegularExpressionMatch &m) { return maskDomain(m.captured(1)) + ":" + m.captured(2); });
    result = replaceAll(result, lookupDomain,
                        [](const QRegularExpressionMatch &m) { return m.captured(1) + " " + maskDomain(m.captured(2)); });
    return replaceAll(result, ipv4, [](const QRegularExpressionMatch &m) {
        const bool isNetworkPrefix = !m.captured(2).isEmpty() && m.captured(1).endsWith(".0");
        return isNetworkPrefix ? m.captured() : maskHost(m.captured(1)) + m.captured(2);
    });
}

Logger &Logger::Instance()
{
    static Logger s;
    return s;
}

bool Logger::init(bool isServiceLogger)
{
    QString path = isServiceLogger ? systemLogDir() : userLogsDir();
    QString logFileName = isServiceLogger ? m_serviceLogFileName : m_logFileName;
    QDir appDir(path);
    if (!appDir.mkpath(path)) {
        return false;
    }

    m_file.setFileName(appDir.filePath(logFileName));
    if (!m_file.open(QIODevice::Append)) {
        qWarning() << "Cannot open log file:" << logFileName;
        return false;
    }

    m_file.setTextModeEnabled(true);
    m_textStream.setDevice(&m_file);

    qInstallMessageHandler(messageHandler);

    return true;
}

void Logger::deInit()
{
    m_textStream.setDevice(nullptr);
    m_file.close();
}

bool Logger::isEnabled()
{
    return m_file.isOpen();
}

bool Logger::setServiceLogsEnabled(bool enabled)
{
#ifdef AMNEZIA_DESKTOP
    return IpcClient::withInterface([enabled](QSharedPointer<IpcInterfaceReplica> iface) {
        iface->setLogsEnabled(enabled);
        qDebug() << "Logger::setServiceLogsEnabled(): Logs transitioned to be " << (enabled ? "enabled" : "disabled");
        return true;
    },[](){
        qWarning() << "Logger::setServiceLogsEnabled(): Service is not running";
        return false;
    });
#endif

    return true;
}

QString Logger::userLogsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/log";
}

QString Logger::systemLogDir()
{
#ifdef Q_OS_WIN
    QStringList locationList = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    QString primaryLocation = "ProgramData";
    foreach (const QString &location, locationList) {
        if (location.contains(primaryLocation)) {
            return QString("%1/%2/log").arg(location).arg(APPLICATION_NAME);
        }
    }
    return QString();
#else
    return QString("/var/log/%1").arg(APPLICATION_NAME);
#endif
}

QString Logger::userLogsFilePath()
{
    return userLogsDir() + QDir::separator() + m_logFileName;
}

QString Logger::serviceLogsFilePath()
{
    return systemLogDir() + QDir::separator() + m_serviceLogFileName;
}

void Logger::clearLogs(bool isServiceLogger)
{
    bool isLogActive = m_file.isOpen();
    m_file.close();

    QFile file(isServiceLogger ? serviceLogsFilePath() : userLogsFilePath());

    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.resize(0);
    file.close();

    if (isLogActive) {
        init(isServiceLogger);
    }
}

void Logger::clearServiceLogs()
{
#ifdef AMNEZIA_DESKTOP
    IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
        iface->clearLogs();
        qDebug() << "Logger::clearServiceLogs(): Logs cleared";
    }, []() {
        qWarning() << "Logger::clearServiceLogs(): Service is not running";
    });
#endif
}

void Logger::cleanUp()
{
    clearLogs(false);
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dir.removeRecursively();

    clearLogs(true);
}

Logger::LogStreamer::LogStreamer(Logger *logger, LogLevel logLevel)
    : m_logger(logger), m_logLevel(logLevel), m_data(new Data())
{
}

Logger::LogStreamer::~LogStreamer()
{
    QString logLevelString;
    switch (m_logLevel) {
    case LogLevel::Trace: logLevelString = "[TRACE]"; break;
    case LogLevel::Debug: logLevelString = "[DEBUG]"; break;
    case LogLevel::Info: logLevelString = "[INFO]"; break;
    case LogLevel::Warning: logLevelString = "[WARNING]"; break;
    case LogLevel::Error: logLevelString = "[ERROR]"; break;
    }

    const QString message = QString("%1 %2 %3 %4 : %5")
                                    .arg(QDateTime::currentDateTimeUtc().toString("[yyyy-MM-dd hh:mm:ss.zzzZ]"),
                                         logLevelString, APPLICATION_NAME, m_logger->className(), maskSensitive(m_data->m_buffer.trimmed()));

    if (m_file.isOpen()) {
        QTextStream logToFile(&m_file);
        logToFile << message << Qt::endl << Qt::flush;
    }

    QTextStream logToOutput((m_logLevel == LogLevel::Error) ? stderr : stdout);
    logToOutput << message << Qt::endl << Qt::flush;

    delete m_data;
}

Logger::LogStreamer Logger::error()
{
    return { this, LogLevel::Error };
}

Logger::LogStreamer Logger::warning()
{
    return { this, LogLevel::Warning };
}

Logger::LogStreamer Logger::info()
{
    return { this, LogLevel::Info };
}

Logger::LogStreamer Logger::debug()
{
    return { this, LogLevel::Debug };
}

QString Logger::sensitive(const QString &input)
{
#ifdef Q_DEBUG
    return input;
#else
    Q_UNUSED(input);
    return { 8, 'X' };
#endif
}

#define CREATE_LOGSTREAMER_OP_REF(x)                                                                                   \
    Logger::LogStreamer &Logger::LogStreamer::operator<<(x t)                                                          \
    {                                                                                                                  \
        m_data->m_ts << t << ' ';                                                                                      \
        return *this;                                                                                                  \
    }

CREATE_LOGSTREAMER_OP_REF(uint64_t);
CREATE_LOGSTREAMER_OP_REF(const char *);
CREATE_LOGSTREAMER_OP_REF(const QString &);
CREATE_LOGSTREAMER_OP_REF(const QByteArray &);
CREATE_LOGSTREAMER_OP_REF(const void *);

#undef CREATE_LOGSTREAMER_OP_REF

Logger::LogStreamer &Logger::LogStreamer::operator<<(const QStringList &t)
{
    m_data->m_ts << '[' << t.join(",") << ']' << ' ';
    return *this;
}

Logger::LogStreamer &Logger::LogStreamer::operator<<(const QJsonObject &t)
{
    m_data->m_ts << QJsonDocument(t).toJson(QJsonDocument::Indented) << ' ';
    return *this;
}

Logger::LogStreamer &Logger::LogStreamer::operator<<(QTextStreamFunction t)
{
    m_data->m_ts << t;
    return *this;
}

void Logger::LogStreamer::addMetaEnum(quint64 value, const QMetaObject *meta, const char *name)
{
    QMetaEnum me = meta->enumerator(meta->indexOfEnumerator(name));

    QString out;
    QTextStream ts(&out);

    if (const char *scope = me.scope()) {
        ts << scope << "::";
    }

    const char *key = me.valueToKey(static_cast<int>(value));
    const bool scoped = me.isScoped();
    if (scoped || !key) {
        ts << me.enumName() << (!key ? "(" : "::");
    }

    if (key) {
        ts << key;
    } else {
        ts << value << ")";
    }

    m_data->m_ts << out;
}
