#include "healthlogclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace kladde {

bool BatchResult::retryable() const
{
    return !ok && (httpStatus == 0 || httpStatus == 429 || httpStatus >= 500);
}

BatchResult parseBatchResponse(int httpStatus, const QByteArray &body)
{
    BatchResult r;
    r.httpStatus = httpStatus;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (httpStatus != 200) {
        const QString code = root.value(QLatin1String("meta")).toObject()
                                 .value(QLatin1String("errorCode")).toString();
        r.error = !code.isEmpty() ? code
                  : !root.value(QLatin1String("error")).toString().isEmpty()
                      ? root.value(QLatin1String("error")).toString()
                      : QStringLiteral("HTTP %1").arg(httpStatus);
        return r;
    }
    const QJsonObject data = root.value(QLatin1String("data")).toObject();
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

QNetworkReply *HealthLogClient::get(const QString &path, bool withToken)
{
    QNetworkRequest req(m_server.resolved(QUrl(path)));
    if (withToken)
        req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    return m_nam->get(req);
}

void HealthLogClient::fetchVersion()
{
    QNetworkReply *reply = get(QStringLiteral("/api/version"), false);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject data =
            QJsonDocument::fromJson(reply->readAll()).object().value(QLatin1String("data")).toObject();
        const QString version = data.value(QLatin1String("version")).toString();
        if (reply->error() != QNetworkReply::NoError || version.isEmpty())
            Q_EMIT versionReceived(false, {}, reply->errorString());
        else
            Q_EMIT versionReceived(true, version, {});
    });
}

void HealthLogClient::fetchMe()
{
    QNetworkReply *reply = get(QStringLiteral("/api/auth/me"), true);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tz = root.value(QLatin1String("data")).toObject()
                               .value(QLatin1String("timezone")).toString();
        const QTimeZone zone(tz.toUtf8());
        if (status == 200 && zone.isValid()) {
            Q_EMIT meReceived(true, zone, {});
            return;
        }
        // A measurements:write token does not reach /api/auth/me (403 auth.scope.insufficient).
        const QString code = root.value(QLatin1String("meta")).toObject()
                                 .value(QLatin1String("errorCode")).toString();
        Q_EMIT meReceived(false, {}, !code.isEmpty() ? code : reply->errorString());
    });
}

void HealthLogClient::postBatch(const Batch &batch)
{
    QNetworkRequest req(m_server.resolved(QUrl(QStringLiteral("/api/measurements/batch"))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    req.setRawHeader("Idempotency-Key", batch.idempotencyKey.toLatin1());
    QNetworkReply *reply = m_nam->post(req, batch.body);
    const QString key = batch.idempotencyKey;
    connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        BatchResult r = parseBatchResponse(status, reply->readAll());
        if (status == 0)
            r.error = reply->errorString();
        Q_EMIT batchFinished(key, r);
    });
}

} // namespace kladde
