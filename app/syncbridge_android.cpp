#include "syncbridge.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJniObject>
#include <QPointer>
#include <QThreadPool>
#include <QtCore/qcoreapplication_platform.h>

// Android: everything goes to de.arianw.kladde.sync.KladdeSync (android/sync), whose
// WorkManager job runs the sync without the app. The app only configures, asks for the
// Health Connect permissions and reads the status the job leaves.

namespace {

constexpr auto Sync = "de/arianw/kladde/sync/KladdeSync";

QJniObject context()
{
    return QNativeInterface::QAndroidApplication::context();
}

QJniObject string(const QString &s)
{
    return QJniObject::fromString(s);
}

} // namespace

void SyncBridge::platformInit()
{
    const QJniObject ctx = context();
    m_configured = QJniObject::callStaticMethod<jboolean>(Sync, "configured", "(Landroid/content/Context;)Z", ctx.object());
    m_interval = int(QJniObject::callStaticMethod<jlong>(Sync, "intervalMinutes", "(Landroid/content/Context;)J", ctx.object()));
    m_backfill = int(QJniObject::callStaticMethod<jlong>(Sync, "backfillDays", "(Landroid/content/Context;)J", ctx.object()));
    // Back from Health Connect's permission screen (or from anywhere): show what changed.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationActive)
            refresh();
    });
}

bool SyncBridge::available() const
{
    return m_demo || QJniObject::callStaticMethod<jboolean>(Sync, "available", "(Landroid/content/Context;)Z", context().object());
}

QString SyncBridge::reason() const
{
    return tr("Health Connect is not installed or not up to date on this phone.");
}

void SyncBridge::storeInterval()
{
    // Rescheduling only once there is something to run.
    if (m_configured)
        QJniObject::callStaticMethod<void>(Sync, "schedule", "(Landroid/content/Context;J)V", context().object(), jlong(m_interval));
}

void SyncBridge::storeBackfill()
{
    QJniObject::callStaticMethod<void>(Sync, "setBackfillDays", "(Landroid/content/Context;J)V", context().object(), jlong(m_backfill));
}

void SyncBridge::configure(const QString &server, const QString &deviceId, Start start)
{
    const QJniObject ctx = context();
    QJniObject::callStaticMethod<void>(Sync, "configure", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Z)V",
                                       ctx.object(), string(server).object<jstring>(), string(deviceId).object<jstring>(),
                                       jboolean(start == Start::Fresh));
    QJniObject::callStaticMethod<void>(Sync, "schedule", "(Landroid/content/Context;J)V", ctx.object(), jlong(m_interval));
    m_configured = true;
    Q_EMIT changed();
    refresh();
}

void SyncBridge::forget()
{
    QJniObject::callStaticMethod<void>(Sync, "forget", "(Landroid/content/Context;)V", context().object());
    m_configured = false;
    m_lastRun = {};
    m_lastSuccess = {};
    m_lastError.clear();
    m_uploaded = 0;
    m_pending = 0;
    m_silent.clear();
    Q_EMIT changed();
}

void SyncBridge::requestPermissions()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject::callStaticMethod<void>(Sync, "requestPermissions", "(Landroid/app/Activity;)V", context().object());
    });
}

void SyncBridge::startSync()
{
    QJniObject::callStaticMethod<void>(Sync, "syncNow", "(Landroid/content/Context;)V", context().object());
}

void SyncBridge::resync()
{
    QJniObject::callStaticMethod<void>(Sync, "resync", "(Landroid/content/Context;)V", context().object());
}

void SyncBridge::refresh()
{
    // Both block (the queue database, Health Connect's permission service): off the UI thread.
    QPointer<SyncBridge> self(this);
    QThreadPool::globalInstance()->start([self] {
        const QJniObject ctx = context();
        const QByteArray status = QJniObject::callStaticMethod<jstring>(
            Sync, "statusJson", "(Landroid/content/Context;)Ljava/lang/String;", ctx.object()).toString().toUtf8();
        const QByteArray permissions = QJniObject::callStaticMethod<jstring>(
            Sync, "permissionsJson", "(Landroid/content/Context;)Ljava/lang/String;", ctx.object()).toString().toUtf8();
        QMetaObject::invokeMethod(qApp, [self, status, permissions] {
            if (self)
                self->applyStatus(status, permissions);
        });
    });
}

void SyncBridge::loadApps()
{
    // Reads Health Connect: off the UI thread.
    QPointer<SyncBridge> self(this);
    QThreadPool::globalInstance()->start([self] {
        const QByteArray apps = QJniObject::callStaticMethod<jstring>(
            Sync, "appsJson", "(Landroid/content/Context;)Ljava/lang/String;", context().object()).toString().toUtf8();
        QMetaObject::invokeMethod(qApp, [self, apps] {
            if (self)
                self->applyApps(apps);
        });
    });
}

void SyncBridge::storeOrder(Ranked kind, const QStringList &apps)
{
    QJniObject::callStaticMethod<void>(Sync, "setAppOrder", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
                                       context().object(), string(keyOf(kind)).object<jstring>(),
                                       string(apps.join(QLatin1Char(','))).object<jstring>());
}
