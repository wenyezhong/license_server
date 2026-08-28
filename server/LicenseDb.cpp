#include "LicenseDb.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>

LicenseDb::LicenseDb() = default;
LicenseDb::~LicenseDb() = default;

#if defined(QDV_DB_MYSQL)

bool LicenseDb::open(const QString& host, quint16 port, const QString& user,
                      const QString& password, const QString& dbName)
{
    m_dbPath = dbName;
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("licDb"));
    db.setHostName(host);
    db.setPort(port);
    db.setUserName(user);
    db.setPassword(password);
    db.setDatabaseName(dbName);
    db.setConnectOptions(QStringLiteral("MYSQL_OPT_RECONNECT=1"));
    if (!db.open()) {
        qWarning() << "无法连接 MySQL 数据库:" << db.lastError().text();
        return false;
    }
    return createTableIfNotExists();
}

bool LicenseDb::createTableIfNotExists()
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    // MySQL 中作为 PRIMARY KEY 的列需要有长度限制的类型（TEXT/BLOB 不可直接建索引）；
    // 带 DEFAULT 的列在旧版 MySQL（< 8.0.13）上也不支持 TEXT/BLOB，改用 VARCHAR。
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS licenses ("
        " machine_code VARCHAR(191) PRIMARY KEY,"
        " license_key  TEXT,"
        " expiry       VARCHAR(32),"
        " customer     TEXT,"
        " features     TEXT,"
        " status       VARCHAR(16) DEFAULT 'active',"
        " activated_count INTEGER DEFAULT 0,"
        " created_at   VARCHAR(32)"
        ") CHARACTER SET utf8mb4");
    if (!q.exec(sql)) {
        qWarning() << "建表失败:" << q.lastError().text();
        return false;
    }

    // 试用记录表（一机一生一次）
    const QString trialSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS trials ("
        " machine_code VARCHAR(191) PRIMARY KEY,"
        " first_trial_date VARCHAR(32),"
        " token            TEXT,"
        " expiry           VARCHAR(32)"
        ") CHARACTER SET utf8mb4");
    if (!q.exec(trialSql)) {
        qWarning() << "建 trials 表失败:" << q.lastError().text();
        return false;
    }
    return true;
}

#else

bool LicenseDb::open(const QString& dbPath)
{
    m_dbPath = dbPath;
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("licDb"));
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "无法打开数据库:" << db.lastError().text();
        return false;
    }
    return createTableIfNotExists();
}

bool LicenseDb::createTableIfNotExists()
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS licenses ("
        " machine_code TEXT PRIMARY KEY,"
        " license_key  TEXT,"
        " expiry       TEXT,"
        " customer     TEXT,"
        " features     TEXT,"
        " status       TEXT DEFAULT 'active',"
        " activated_count INTEGER DEFAULT 0,"
        " created_at   TEXT)");
    if (!q.exec(sql)) {
        qWarning() << "建表失败:" << q.lastError().text();
        return false;
    }

    // 试记录表（一机一生一次）
    const QString trialSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS trials ("
        " machine_code TEXT PRIMARY KEY,"
        " first_trial_date TEXT,"
        " token            TEXT,"
        " expiry           TEXT)");
    if (!q.exec(trialSql)) {
        qWarning() << "建 trials 表失败:" << q.lastError().text();
        return false;
    }
    return true;
}

#endif

bool LicenseDb::find(const QString& machineCode, QJsonObject* out) const
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    q.prepare(QStringLiteral("SELECT machine_code, license_key, expiry, customer, features, status, activated_count, created_at "
                             "FROM licenses WHERE machine_code = ?"));
    q.addBindValue(machineCode);
    if (!q.exec() || !q.next())
        return false;

    QJsonObject o;
    o.insert(QStringLiteral("machine_code"), q.value(0).toString());
    o.insert(QStringLiteral("license_key"), q.value(1).toString());
    o.insert(QStringLiteral("expiry"), q.value(2).toString());
    o.insert(QStringLiteral("customer"), q.value(3).toString());
    o.insert(QStringLiteral("features"), q.value(4).toString());
    o.insert(QStringLiteral("status"), q.value(5).toString());
    o.insert(QStringLiteral("activated_count"), q.value(6).toInt());
    o.insert(QStringLiteral("created_at"), q.value(7).toString());
    if (out) *out = o;
    return true;
}

bool LicenseDb::upsert(const QJsonObject& rec)
{
    const QString machine = rec.value(QStringLiteral("machine_code")).toString();
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("licDb"));

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT machine_code FROM licenses WHERE machine_code = ?"));
    q.addBindValue(machine);
    const bool exists = q.exec() && q.next();

    QSqlQuery w(db);
    if (exists) {
        w.prepare(QStringLiteral(
            "UPDATE licenses SET license_key=?, expiry=?, customer=?, features=?, status=?, "
            "activated_count = activated_count + 1 WHERE machine_code=?"));
    } else {
        w.prepare(QStringLiteral(
            "INSERT INTO licenses (machine_code, license_key, expiry, customer, features, status, activated_count, created_at) "
            "VALUES (?,?,?,?,?,?,1,?)"));
        w.addBindValue(machine);
    }
    w.addBindValue(rec.value(QStringLiteral("license_key")).toString());
    w.addBindValue(rec.value(QStringLiteral("expiry")).toString());
    w.addBindValue(rec.value(QStringLiteral("customer")).toString());
    w.addBindValue(rec.value(QStringLiteral("features")).toString());
    w.addBindValue(rec.value(QStringLiteral("status")).toString());
    if (exists) {
        w.addBindValue(machine);
    } else {
        w.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    }
    return w.exec();
}

int LicenseDb::count() const
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM licenses")) && q.next())
        return q.value(0).toInt();
    return 0;
}

QJsonArray LicenseDb::listAll() const
{
    QJsonArray arr;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    if (!q.exec(QStringLiteral("SELECT machine_code, expiry, customer, features, status, activated_count, created_at FROM licenses ORDER BY created_at DESC")))
        return arr;
    while (q.next()) {
        QJsonObject o;
        o.insert(QStringLiteral("machine_code"), q.value(0).toString());
        o.insert(QStringLiteral("expiry"), q.value(1).toString());
        o.insert(QStringLiteral("customer"), q.value(2).toString());
        o.insert(QStringLiteral("features"), q.value(3).toString());
        o.insert(QStringLiteral("status"), q.value(4).toString());
        o.insert(QStringLiteral("activated_count"), q.value(5).toInt());
        o.insert(QStringLiteral("created_at"), q.value(6).toString());
        arr.append(o);
    }
    return arr;
}

bool LicenseDb::query(const QString& machineCode, QJsonObject* out) const
{
    return find(machineCode, out);
}

bool LicenseDb::findTrial(const QString& machineCode, QJsonObject* out) const
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    q.prepare(QStringLiteral("SELECT machine_code, first_trial_date, token, expiry "
                             "FROM trials WHERE machine_code = ?"));
    q.addBindValue(machineCode);
    if (!q.exec() || !q.next())
        return false;

    QJsonObject o;
    o.insert(QStringLiteral("machine_code"), q.value(0).toString());
    o.insert(QStringLiteral("first_trial_date"), q.value(1).toString());
    o.insert(QStringLiteral("token"), q.value(2).toString());
    o.insert(QStringLiteral("expiry"), q.value(3).toString());
    if (out) *out = o;
    return true;
}

#if defined(QDV_DB_MYSQL)
static const QLatin1String kInsertIgnoreTrialSql(
    "INSERT IGNORE INTO trials (machine_code, first_trial_date, token, expiry) VALUES (?,?,?,?)");
#else
static const QLatin1String kInsertIgnoreTrialSql(
    "INSERT OR IGNORE INTO trials (machine_code, first_trial_date, token, expiry) VALUES (?,?,?,?)");
#endif

bool LicenseDb::insertTrial(const QString& machineCode, const QString& firstTrialDate,
                            const QString& token, const QString& expiry)
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    q.prepare(kInsertIgnoreTrialSql);
    q.addBindValue(machineCode);
    q.addBindValue(firstTrialDate);
    q.addBindValue(token);
    q.addBindValue(expiry);
    return q.exec();
}

bool LicenseDb::upsertTrial(const QJsonObject& rec)
{
    const QString machine = rec.value(QStringLiteral("machine_code")).toString();
    const QString token = rec.value(QStringLiteral("token")).toString();
    const QString expiry = rec.value(QStringLiteral("expiry")).toString();
    const QString today = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd"));

    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    q.prepare(kInsertIgnoreTrialSql);
    q.addBindValue(machine);
    q.addBindValue(today);
    q.addBindValue(token);
    q.addBindValue(expiry);
    return q.exec();
}

bool LicenseDb::incrementActivated(const QString& machineCode)
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("licDb")));
    q.prepare(QStringLiteral("UPDATE licenses SET activated_count = activated_count + 1 "
                             "WHERE machine_code = ?"));
    q.addBindValue(machineCode);
    return q.exec();
}