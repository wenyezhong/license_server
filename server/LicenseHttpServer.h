#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHostAddress>

class LicenseDb;

/// 供 handleRequest 使用（简单全局注入）
void setGlobalLicenseDb(LicenseDb* db);

/**
 * @brief 授权 HTTP 服务器（基于 QTcpServer 的最小实现）
 *
 * 接口：
 *   POST /activate  body={"machine","app","version"}
 *      → 查找该机器码授权记录，返回 {"license_key","expiry","status"} 或 {"license_key":"","message"}
 *   POST /register  body={"machine_code","license_key","expiry","customer","features"}
 *      → 登记/更新授权记录（由授权码生成工具调用），返回 {"ok":true}
 *   POST /register-trial body={"machine_code","token","expiry","customer","features"}
 *      → 登记试用记录（由授权码生成工具调用，一机一生一次），返回 {"ok":true}
 *   POST /trial     body={"machine","app","version"}
 *      → 查询该机器码试用记录，返回 {"ok":true,"token","expiry"} 或 {"ok":false,"message"}
 *   GET  /health    → 探活 {"ok":true,"count":N}
 */
class LicenseHttpServer : public QObject
{
    Q_OBJECT
public:
    explicit LicenseHttpServer(QObject* parent = nullptr);

    bool listen(quint16 port);

signals:
    /// 服务器开始监听
    void started();
    /// 记录日志
    void log(const QString& msg);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void writeResponse(QTcpSocket* socket, int status, const QByteArray& json);
    QByteArray handleRequest(const QByteArray& method, const QString& path, const QByteArray& body);

    QTcpServer m_server;
};