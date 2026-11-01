#include "sessionvault.h"

#include <QJniObject>
#include <QJsonDocument>
#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <QtCore/qcoreapplication_platform.h>

namespace {

constexpr auto Sync = "de/arianw/kladde/sync/KladdeSync";
// Which server's session the vault holds (the SecretStore key).
const auto VaultKey = QStringLiteral("vaultKey");

QJniObject context()
{
    return QNativeInterface::QAndroidApplication::context();
}

} // namespace

SessionVault::SessionVault(QObject *parent)
    : QObject(parent)
{
}

void SessionVault::read(const QString &key, ReadDone done)
{
    if (QSettings().value(VaultKey).toString() != key) {
        done(false, {});
        return;
    }
    const QString token = QJniObject::callStaticMethod<jstring>(Sync, "refreshToken", "(Landroid/content/Context;)Ljava/lang/String;",
                                                                context().object()).toString();
    done(!token.isEmpty(), token);
}

void SessionVault::write(const QString &key, const QString &value, Done done)
{
    QSettings().setValue(VaultKey, key);
    QJniObject::callStaticMethod<void>(Sync, "begin", "(Landroid/content/Context;Ljava/lang/String;)V",
                                       context().object(), QJniObject::fromString(value).object<jstring>());
    done(true);
}

void SessionVault::remove(const QString &, Done done)
{
    QSettings().remove(VaultKey);
    QJniObject::callStaticMethod<void>(Sync, "endSession", "(Landroid/content/Context;)V", context().object());
    done(true);
}

void SessionVault::renew(const QString &stale, kladde::Session::Callback done)
{
    // The vault may wait for a renewal of the background job and then go to the network.
    QPointer<SessionVault> self(this);
    QThreadPool::globalInstance()->start([self, stale, done = std::move(done)] {
        const QString json = QJniObject::callStaticMethod<jstring>(
            Sync, "renewJson", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
            context().object(), QJniObject::fromString(stale).object<jstring>()).toString();
        if (!self) {
            return;
        }
        // {"status": 200, "body": "{\"data\": {...}}"}, like POST /api/auth/refresh.
        QMetaObject::invokeMethod(self, [json, done] {
            const QJsonObject answer = QJsonDocument::fromJson(json.toUtf8()).object();
            kladde::ApiReply r;
            r.status = answer.value(QLatin1String("status")).toInt();
            r.root = QJsonDocument::fromJson(answer.value(QLatin1String("body")).toString().toUtf8()).object();
            done(r);
        }, Qt::QueuedConnection);
    });
}
