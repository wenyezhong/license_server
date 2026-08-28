#include <QCoreApplication>
#include <QCommandLineParser>

#include "LicenseDb.h"
#include "LicenseHttpServer.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QDVision-LicenseServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QDVision 授权服务器"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption portOpt(QStringLiteral("port"), QStringLiteral("监听端口"), QStringLiteral("port"), QStringLiteral("8899"));
    parser.addOption(portOpt);

#if defined(QDV_DB_MYSQL)
    QCommandLineOption hostOpt(QStringLiteral("db-host"), QStringLiteral("MySQL 主机地址"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption dbPortOpt(QStringLiteral("db-port"), QStringLiteral("MySQL 端口"), QStringLiteral("port"), QStringLiteral("3306"));
    QCommandLineOption userOpt(QStringLiteral("db-user"), QStringLiteral("MySQL 用户名"), QStringLiteral("user"), QStringLiteral("root"));
    QCommandLineOption passwordOpt(QStringLiteral("db-password"), QStringLiteral("MySQL 密码"), QStringLiteral("password"), QStringLiteral(""));
    QCommandLineOption nameOpt(QStringLiteral("db-name"), QStringLiteral("MySQL 数据库名"), QStringLiteral("name"), QStringLiteral("license_server"));
    parser.addOption(hostOpt);
    parser.addOption(dbPortOpt);
    parser.addOption(userOpt);
    parser.addOption(passwordOpt);
    parser.addOption(nameOpt);
#else
    QCommandLineOption dbOpt(QStringLiteral("db"), QStringLiteral("数据库文件路径"), QStringLiteral("db"), QStringLiteral("license.db"));
    parser.addOption(dbOpt);
#endif

    parser.process(app);

    const quint16 port = quint16(parser.value(portOpt).toUShort());

    LicenseDb db;
#if defined(QDV_DB_MYSQL)
    const QString host = parser.value(hostOpt);
    const quint16 dbPort = quint16(parser.value(dbPortOpt).toUShort());
    const QString user = parser.value(userOpt);
    const QString password = parser.value(passwordOpt);
    const QString dbName = parser.value(nameOpt);
    if (!db.open(host, dbPort, user, password, dbName)) {
        qCritical("无法连接 MySQL 数据库 %s@%s:%u/%s",
                  qPrintable(user), qPrintable(host), dbPort, qPrintable(dbName));
        return 1;
    }
#else
    const QString dbPath = parser.value(dbOpt);
    if (!db.open(dbPath)) {
        qCritical("无法打开数据库 %s", qPrintable(dbPath));
        return 1;
    }
#endif
    setGlobalLicenseDb(&db);

    LicenseHttpServer server;
    QObject::connect(&server, &LicenseHttpServer::log,
                     [](const QString& m) { qInfo().noquote() << m; });
    if (!server.listen(port)) {
        qCritical("监听端口 %u 失败", port);
        return 1;
    }
#if defined(QDV_DB_MYSQL)
    qInfo().noquote() << QStringLiteral("授权服务器已启动，监听端口 %1，MySQL 数据库 %2@%3:%4/%5")
                              .arg(port).arg(user, host).arg(dbPort).arg(dbName);
#else
    qInfo().noquote() << QStringLiteral("授权服务器已启动，监听端口 %1，数据库 %2").arg(port).arg(dbPath);
#endif
    qInfo().noquote() << QStringLiteral("已授权机器数: %1").arg(db.count());

    return app.exec();
}