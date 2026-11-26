#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QNetworkRequest;

namespace kladde {

// One answer of the HealthLog API: {data, error, meta}.
struct ApiReply {
    int status = 0; // 0: no answer (network, TLS, timeout)
    QJsonObject root;
    QString transportError;

    bool ok() const { return status >= 200 && status < 300; }
    QJsonValue data() const { return root.value(QLatin1String("data")); }
    // meta.errorCode, else the error text, else the transport error or "HTTP <status>".
    // For people: the app's Errors::describe, translated.
    QString error() const;
};

// A request to HealthLog with what every call needs: transfer timeout, JSON, the app's
// identity, no cookies. Callers add their own headers (token, Idempotency-Key).
QNetworkRequest apiRequest(const QUrl &url);

// The finished reply as an ApiReply. Does not delete it.
ApiReply readReply(QNetworkReply *reply);

} // namespace kladde
