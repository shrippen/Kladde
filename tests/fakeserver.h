#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>

// A tiny HTTP/1.1 server on 127.0.0.1 for the client tests: one request per connection,
// answers from a handler, records what it got.
class FakeServer : public QObject
{
    Q_OBJECT
public:
    struct Request {
        QByteArray verb;
        QString path;
        QHash<QByteArray, QByteArray> headers; // lower-case names
        QJsonObject body;
    };
    struct Answer {
        int status = 200;
        QJsonObject body;
    };
    using Handler = std::function<Answer(const Request &)>;

    explicit FakeServer(Handler handler, QObject *parent = nullptr)
        : QObject(parent)
        , m_handler(std::move(handler))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                serve(s);
        });
        m_server.listen(QHostAddress::LocalHost);
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }
    QList<Request> requests;

private:
    void serve(QTcpSocket *socket)
    {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            *buffer += socket->readAll();
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            Request req;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            req.verb = first.value(0);
            req.path = QString::fromLatin1(first.value(1));
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const qsizetype colon = lines[i].indexOf(':');
                if (colon > 0)
                    req.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
            }
            const qsizetype length = req.headers.value("content-length").toLongLong();
            if (buffer->size() < end + 4 + length)
                return;
            req.body = QJsonDocument::fromJson(buffer->mid(end + 4, length)).object();
            requests << req;

            const Answer a = m_handler(req);
            const QByteArray body = QJsonDocument(a.body).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 " + QByteArray::number(a.status) + " X\r\nContent-Type: application/json\r\n"
                          "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
            buffer->clear();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    QTcpServer m_server;
    Handler m_handler;
};
