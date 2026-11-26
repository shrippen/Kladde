#include "http.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace kladde {

namespace {

constexpr int TransferTimeoutMs = 30000;

// e.g. "Kladde/0.1.0"
QString userAgent()
{
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral("Kladde") : QStringLiteral("Kladde/") + version;
}

} // namespace

QString ApiReply::error() const
{
    const QString code = root.value(QLatin1String("meta")).toObject()
                             .value(QLatin1String("errorCode")).toString();
    if (!code.isEmpty())
        return code;
    const QJsonValue e = root.value(QLatin1String("error"));
    if (e.isString() && !e.toString().isEmpty())
        return e.toString();
    if (e.isObject() && !e.toObject().value(QLatin1String("message")).toString().isEmpty())
        return e.toObject().value(QLatin1String("message")).toString();
    if (status == 0)
        return transportError.isEmpty() ? QStringLiteral("no connection") : transportError;
    return QStringLiteral("HTTP %1").arg(status);
}

QNetworkRequest apiRequest(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(TransferTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Client-Type", "native");
    // Not Qt's "Mozilla/5.0" and no cookies: HealthLog takes either for a browser and
    // gives it no refresh token.
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    return req;
}

ApiReply readReply(QNetworkReply *reply)
{
    ApiReply r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.root = QJsonDocument::fromJson(reply->readAll()).object();
    if (r.status == 0) {
        r.transportError = reply->errorString();
    }
    return r;
}

} // namespace kladde
