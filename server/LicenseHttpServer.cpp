#include "LicenseHttpServer.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QUrl>
#include <QUrlQuery>

#include "LicenseDb.h"

#include <QDateTime>
#include <QJsonDocument>

// 每个连接保留的接收缓冲区
static QHash<QTcpSocket*, QByteArray>& buffers()
{
    static QHash<QTcpSocket*, QByteArray> b;
    return b;
}

namespace {
LicenseDb* g_db = nullptr;
}

LicenseHttpServer::LicenseHttpServer(QObject* parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &LicenseHttpServer::onNewConnection);
}

bool LicenseHttpServer::listen(quint16 port)
{
    return m_server.listen(QHostAddress::Any, port);
}

void LicenseHttpServer::onNewConnection()
{
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        buffers().insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [socket] {
            buffers().remove(socket);
            socket->deleteLater();
        });
    }
}

// 数据到达后累积，直到收齐 请求头 + Content-Length 长度的 body 再处理
void LicenseHttpServer::onReadyRead(QTcpSocket* socket)
{
    QByteArray& buf = buffers()[socket];
    buf.append(socket->readAll());

    // 找请求头结束位置
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return; // 头还没收全

    // 解析 Content-Length
    int contentLength = 0;
    const QByteArray header = buf.left(headerEnd);
    const QList<QByteArray> lines = header.split('\n');
    for (const QByteArray& l : lines) {
        const QString line = QString::fromLatin1(l).trimmed();
        if (line.startsWith(QStringLiteral("Content-Length:"), Qt::CaseInsensitive))
            contentLength = line.mid(15).trimmed().toInt();
    }

    // 若 body 未收齐，等待
    if (buf.size() < headerEnd + 4 + contentLength)
        return;

    // 解析请求行
    QByteArray method, rawPath;
    if (!lines.isEmpty()) {
        const QList<QByteArray> head = lines[0].trimmed().split(' ');
        if (head.size() >= 2) {
            method = head[0];
            rawPath = head[1];
        }
    }
    const QString path = QString::fromLatin1(rawPath).split('?').first();

    // 提取 body
    const QByteArray body = buf.mid(headerEnd + 4, contentLength);

    const QByteArray resp = handleRequest(method, path, body);
    writeResponse(socket, 200, resp);
    socket->disconnectFromHost();
}

QByteArray LicenseHttpServer::handleRequest(const QByteArray& method, const QString& path,
                                            const QByteArray& body)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();

    if (path == QStringLiteral("/health")) {
        QJsonObject r;
        r.insert(QStringLiteral("ok"), true);
        r.insert(QStringLiteral("count"), g_db ? g_db->count() : 0);
        return QJsonDocument(r).toJson(QJsonDocument::Compact);
    }

    // 已激活清单
    if (path == QStringLiteral("/list")) {
        QJsonObject r;
        r.insert(QStringLiteral("ok"), true);
        r.insert(QStringLiteral("count"), g_db ? g_db->count() : 0);
        r.insert(QStringLiteral("licenses"), g_db ? g_db->listAll() : QJsonArray());
        return QJsonDocument(r).toJson(QJsonDocument::Compact);
    }

    // 查询单个机器码的激活状态
    if (path == QStringLiteral("/query")) {
        const QString machine = obj.value(QStringLiteral("machine_code")).toString();
        if (machine.isEmpty())
            return R"({"ok":false,"message":"missing machine_code"})";
        QJsonObject rec;
        if (g_db && g_db->query(machine, &rec)) {
            QJsonObject r;
            r.insert(QStringLiteral("ok"), true);
            r.insert(QStringLiteral("found"), true);
            r.insert(QStringLiteral("data"), rec);
            return QJsonDocument(r).toJson(QJsonDocument::Compact);
        }
        QJsonObject r;
        r.insert(QStringLiteral("ok"), true);
        r.insert(QStringLiteral("found"), false);
        r.insert(QStringLiteral("message"), QStringLiteral("该机器码未找到授权记录"));
        return QJsonDocument(r).toJson(QJsonDocument::Compact);
    }

    if (path == QStringLiteral("/activate")) {
        const QString machine = obj.value(QStringLiteral("machine")).toString();
        if (machine.isEmpty())
            return R"({"ok":false,"message":"missing machine"})";

        QJsonObject rec;
        if (g_db && g_db->find(machine, &rec)) {
            if (rec.value(QStringLiteral("status")).toString() == QStringLiteral("disabled")) {
                QJsonObject r;
                r.insert(QStringLiteral("ok"), false);
                r.insert(QStringLiteral("license_key"), QString());
                r.insert(QStringLiteral("message"), QStringLiteral("该授权已被禁用"));
                return QJsonDocument(r).toJson(QJsonDocument::Compact);
            }
            // 每次激活计数 +1
            g_db->incrementActivated(machine);
            emit log(QStringLiteral("激活: %1, 已授权机器数: %2")
                     .arg(machine, QString::number(g_db->count())));
            QJsonObject r;
            r.insert(QStringLiteral("ok"), true);
            r.insert(QStringLiteral("license_key"), rec.value(QStringLiteral("license_key")).toString());
            r.insert(QStringLiteral("expiry"), rec.value(QStringLiteral("expiry")).toString());
            r.insert(QStringLiteral("customer"), rec.value(QStringLiteral("customer")).toString());
            return QJsonDocument(r).toJson(QJsonDocument::Compact);
        }
        QJsonObject r;
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("license_key"), QString());
        r.insert(QStringLiteral("message"), QStringLiteral("该机器码未授权，请联系销售方"));
        return QJsonDocument(r).toJson(QJsonDocument::Compact);
    }

    if (path == QStringLiteral("/trial")) {
        // 纯数据库查询：试用 token 由 license_tool 离线预签名后登记到服务器
        const QString machine = obj.value(QStringLiteral("machine")).toString();
        if (machine.isEmpty())
            return R"({"ok":false,"message":"missing machine"})";

        QJsonObject rec;
        if (g_db && g_db->findTrial(machine, &rec)) {
            QJsonObject r;
            r.insert(QStringLiteral("ok"), true);
            r.insert(QStringLiteral("token"), rec.value(QStringLiteral("token")).toString());
            r.insert(QStringLiteral("expiry"), rec.value(QStringLiteral("expiry")).toString());
            return QJsonDocument(r).toJson(QJsonDocument::Compact);
        }
        QJsonObject r;
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("message"), QStringLiteral("该机器码无试用授权，请联系销售方"));
        return QJsonDocument(r).toJson(QJsonDocument::Compact);
    }

    if (path == QStringLiteral("/register")) {
        const QString machine = obj.value(QStringLiteral("machine_code")).toString();
        const QString key = obj.value(QStringLiteral("license_key")).toString();
        if (machine.isEmpty() || key.isEmpty())
            return R"({"ok":false,"message":"missing machine_code or license_key"})";

        if (g_db) {
            g_db->upsert(obj);
            emit log(QStringLiteral("登记授权码: %1").arg(machine));
            QJsonObject r;
            r.insert(QStringLiteral("ok"), true);
            r.insert(QStringLiteral("count"), g_db->count());
            return QJsonDocument(r).toJson(QJsonDocument::Compact);
        }
        return R"({"ok":false,"message":"db not ready"})";
    }

    if (path == QStringLiteral("/register-trial")) {
        const QString machine = obj.value(QStringLiteral("machine_code")).toString();
        const QString token = obj.value(QStringLiteral("token")).toString();
        if (machine.isEmpty() || token.isEmpty())
            return R"({"ok":false,"message":"missing machine_code or token"})";

        if (g_db) {
            g_db->upsertTrial(obj);
            emit log(QStringLiteral("登记试用码: %1").arg(machine));
            QJsonObject r;
            r.insert(QStringLiteral("ok"), true);
            return QJsonDocument(r).toJson(QJsonDocument::Compact);
        }
        return R"({"ok":false,"message":"db not ready"})";
    }

    QJsonObject r;
    r.insert(QStringLiteral("ok"), false);
    r.insert(QStringLiteral("message"), QStringLiteral("未知接口: %1").arg(path));
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}

void LicenseHttpServer::writeResponse(QTcpSocket* socket, int status, const QByteArray& json)
{
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(status) +
            (status == 200 ? " OK" : " Error") + "\r\n";
    resp += "Content-Type: application/json\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    resp += "Content-Length: " + QByteArray::number(json.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += json;
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

void setGlobalLicenseDb(LicenseDb* db) { g_db = db; }