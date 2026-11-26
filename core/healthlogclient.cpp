#include "healthlogclient.h"
#include "http.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace kladde {

namespace {

constexpr int HttpOk = 200;
constexpr int HttpConflict = 409;
constexpr int HttpTooManyRequests = 429;
constexpr int HttpServerError = 500;

} // namespace

bool BatchResult::retryable() const
{
    return !ok && (httpStatus == 0 || httpStatus == HttpConflict || httpStatus == HttpTooManyRequests
                   || httpStatus >= HttpServerError);
}

namespace {

BatchResult batchResult(const ApiReply &reply)
{
    BatchResult r;
    r.httpStatus = reply.status;
    if (reply.status != HttpOk) {
        r.error = reply.error();
        return r;
    }
    const QJsonObject data = reply.data().toObject();
    if (data.isEmpty()) {
        r.error = QStringLiteral("answer without data");
        return r;
    }
    r.ok = true;
    r.processed = data.value(QLatin1String("processed")).toInt();
    r.inserted = data.value(QLatin1String("inserted")).toInt();
    r.updated = data.value(QLatin1String("updated")).toInt();
    r.duplicates = data.value(QLatin1String("duplicates")).toInt();
    r.failed = data.value(QLatin1String("failed")).toInt();
    for (const QJsonValue &v : data.value(QLatin1String("skipped")).toArray()) {
        const QJsonObject s = v.toObject();
        r.skipped << QStringLiteral("%1: %2")
                         .arg(s.value(QLatin1String("index")).toInt())
                         .arg(s.value(QLatin1String("reason")).toString());
    }
    return r;
}

} // namespace

BatchResult parseBatchResponse(int httpStatus, const QByteArray &body)
{
    ApiReply reply;
    reply.status = httpStatus;
    reply.root = QJsonDocument::fromJson(body).object();
    return batchResult(reply);
}

HealthLogClient::HealthLogClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void HealthLogClient::setServer(const QUrl &server)
{
    m_server = server;
}

void HealthLogClient::setToken(const QString &token)
{
    m_token = token;
}

QNetworkReply *HealthLogClient::get(const QString &path)
{
    QNetworkRequest req = apiRequest(m_server.resolved(QUrl(path)));
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    return m_nam->get(req);
}

void HealthLogClient::fetchMe()
{
    QNetworkReply *reply = get(QStringLiteral("/api/auth/me"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const ApiReply r = readReply(reply);
        const QString tz = r.data().toObject().value(QLatin1String("timezone")).toString();
        const QTimeZone zone(tz.toUtf8());
        if (r.status == HttpOk && zone.isValid()) {
            Q_EMIT meReceived(true, zone, {});
            return;
        }
        // A measurements:write token does not reach /api/auth/me (403 auth.scope.insufficient).
        Q_EMIT meReceived(false, {}, r.error());
    });
}

void HealthLogClient::postBatch(const Batch &batch)
{
    QNetworkRequest req = apiRequest(m_server.resolved(QUrl(QStringLiteral("/api/measurements/batch"))));
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    req.setRawHeader("Idempotency-Key", batch.idempotencyKey.toLatin1());
    QNetworkReply *reply = m_nam->post(req, batch.body);
    const QString key = batch.idempotencyKey;
    connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
        reply->deleteLater();
        Q_EMIT batchFinished(key, batchResult(readReply(reply)));
    });
}

} // namespace kladde
