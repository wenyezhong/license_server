#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief 授权数据库
 *
 * Windows 版本（QDV_DB_SQLITE）使用 SQLite 本地文件；
 * Linux 版本（QDV_DB_MYSQL）使用 MySQL；由 license_server.pro 按平台
 * 条件编译（DEFINES）自动切换，二者对外接口一致，业务代码无需区分。
 *
 * 存储每一台已授权机器的授权记录：
 *  机器码、授权码（签名后的）、到期时间、客户、功能、状态、激活次数、创建时间。
 *
 * 表 licenses:
 *   machine_code TEXT/VARCHAR PRIMARY KEY
 *   license_key  TEXT
 *   expiry       TEXT (yyyy-MM-dd)
 *   customer     TEXT
 *   features     TEXT
 *   status       TEXT/VARCHAR (active/disabled)
 *   activated_count INTEGER
 *   created_at   TEXT
 *
 * 表 trials（试用记录，一机一生一次）:
 *   machine_code     TEXT/VARCHAR PRIMARY KEY
 *   first_trial_date TEXT  首次试用日期 (yyyy-MM-dd)
 *   token            TEXT  签名的试用 token
 *   expiry           TEXT  试用到期日 (yyyy-MM-dd)
 */
class LicenseDb
{
public:
    LicenseDb();
    ~LicenseDb();

#if defined(QDV_DB_MYSQL)
    /// 连接 MySQL 数据库（不存在的表会自动创建）。返回是否成功。
    bool open(const QString& host, quint16 port, const QString& user,
              const QString& password, const QString& dbName);
#else
    /// 打开 SQLite 数据库文件（不存在则创建）。返回是否成功。
    bool open(const QString& dbPath);
#endif

    /// 查询某机器码的授权记录；有则用 out 返回并返回 true
    bool find(const QString& machineCode, QJsonObject* out) const;

    /// 新增/更新授权记录（upsert）。返回是否成功。
    bool upsert(const QJsonObject& record);

    /// 统计已激活机器数量
    int count() const;

    /// 返回所有授权记录列表
    QJsonArray listAll() const;

    /// 查询指定机器码的激活信息（含完整字段）
    bool query(const QString& machineCode, QJsonObject* out) const;

    /// 查询某机器码的试用记录；有则用 out 返回并返回 true
    bool findTrial(const QString& machineCode, QJsonObject* out) const;

    /// 新增试用记录（只 insert，不 update，保证首次日期与原 token 不可变）
    bool insertTrial(const QString& machineCode, const QString& firstTrialDate,
                     const QString& token, const QString& expiry);

    /// 登记试用记录（INSERT OR IGNORE，与 /register 类似的 JSON 接口）
    /// 入参: {"machine_code","token","expiry","customer","features"}
    bool upsertTrial(const QJsonObject& rec);

    /// 激活计数 +1（/activate 每次调用时递增）
    bool incrementActivated(const QString& machineCode);

private:
    bool createTableIfNotExists();

    QString m_dbPath;
};