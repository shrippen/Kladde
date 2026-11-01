#pragma once

#include "batch.h"

#include <QObject>
#include <QTimeZone>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace kladde {

// What POST /api/measurements/batch answered.
struct BatchResult {
    bool ok = false;
    int httpStatus = 0;
    QString error; // transport error or the server's error code
    int processed = 0;
    int inserted = 0;
    int updated = 0;
    int duplicates = 0;
    int failed = 0;
    QStringList skipped; // "index: reason"

    // Whether sending the same batch again later can succeed (network trouble, 429, 5xx).
    // A 4xx other than 429 is permanent for the batch as sent.
    bool retryable() const;
};

BatchResult parseBatchResponse(int httpStatus, const QByteArray &body);

// Talks to one HealthLog instance with a Bearer token.
class HealthLogClient : public QObject
{
    Q_OBJECT
public:
    explicit HealthLogClient(QObject *parent = nullptr);

    void setServer(const QUrl &server);
    void setToken(const QString &token);

    // GET /api/auth/me: the account's time zone (needed for day totals). Emits meReceived.
    void fetchMe();
    // GET /api/version (no token needed). Emits versionReceived.
    void fetchVersion();
    // POST /api/measurements/batch. Emits batchFinished with the batch's key.
    void postBatch(const Batch &batch);

Q_SIGNALS:
    void meReceived(bool ok, const QTimeZone &zone, const QString &error);
    void versionReceived(bool ok, const QString &version, const QString &error);
    void batchFinished(const QString &idempotencyKey, const kladde::BatchResult &result);

private:
    QNetworkReply *get(const QString &path, bool withToken);

    QNetworkAccessManager *m_nam;
    QUrl m_server;
    QString m_token;
};

} // namespace kladde
