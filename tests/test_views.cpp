#include "account.h"
#include "dashboard.h"
#include "history.h"
#include "secretstore.h"
#include "syncbridge.h"
#include "syncbridge_fake.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>

using kladde::MemorySecretStore;
using kladde::Session;

// The app's pages with their real C++ objects, the server held back by the test.
class TestViews : public QObject
{
    Q_OBJECT

    // The QML of a page, straight from the sources (Kante next to it).
    static QUrl page(const char *name)
    {
        return QUrl::fromLocalFile(QStringLiteral(KLADDE_QML "/") + QLatin1String(name));
    }

    // Every item below `root` in the visual tree (a Repeater's items have no QObject parent).
    static QList<QQuickItem *> itemsBelow(QQuickItem *root)
    {
        QList<QQuickItem *> out;
        for (QQuickItem *child : root->childItems()) {
            out << child << itemsBelow(child);
        }
        return out;
    }

    // Kante's loader somewhere below `root`, shown.
    static bool showsLoader(QQuickItem *root)
    {
        const auto items = root->findChildren<QQuickItem *>();
        return std::any_of(items.begin(), items.end(), [](QQuickItem *i) {
            return QByteArray(i->metaObject()->className()).startsWith("KanteLoader") && i->isVisible();
        });
    }

private Q_SLOTS:
    void homeShowsItIsLoading()
    {
        // Seen on the phone, 1.11.2026: the first load took 30 s (until the time-out) on an
        // empty page, as if there was nothing.
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setTransport([](const QByteArray &, const QString &, const QByteArray &, Session::Callback) {});
        Dashboard dashboard(&session);

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(KLADDE_QML));
        engine.rootContext()->setContextProperty(QStringLiteral("dashboard"), &dashboard);
        QQmlComponent component(&engine, page("HomeView.qml"));
        std::unique_ptr<QObject> view(component.create());
        QVERIFY2(view, qPrintable(component.errorString()));

        QVERIFY(dashboard.loading());
        QVERIFY(showsLoader(qobject_cast<QQuickItem *>(view.get())));
    }

    void historyShowsItIsLoading()
    {
        MemorySecretStore secrets;
        Session session(&secrets);
        session.setTransport([](const QByteArray &, const QString &, const QByteArray &, Session::Callback) {});
        History history(&session);

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(KLADDE_QML));
        engine.rootContext()->setContextProperty(QStringLiteral("history"), &history);
        QQmlComponent component(&engine, page("HistoryView.qml"));
        std::unique_ptr<QObject> view(component.create());
        QVERIFY2(view, qPrintable(component.errorString()));

        QVERIFY(history.loading());
        QVERIFY(showsLoader(qobject_cast<QQuickItem *>(view.get())));
    }

    void moreShowsTheAppOrder()
    {
        FakeSync::apps = R"({"sleep": [{"app": "coredevices.coreapp", "label": "Pebble"},
                                       {"app": "com.urbandroid.sleep", "label": "Sleep as Android"}],
                             "workouts": [{"app": "com.freeletics.lite", "label": "Freeletics"}]})";
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        qmlRegisterUncreatableType<SyncBridge>("Kladde.Core", 1, 0, "SyncBridge", QStringLiteral("test"));

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(KLADDE_QML));
        engine.rootContext()->setContextProperty(QStringLiteral("sync"), &sync);
        engine.rootContext()->setContextProperty(QStringLiteral("account"), &account);
        QQmlComponent component(&engine, page("MoreView.qml"));
        std::unique_ptr<QObject> view(component.create());
        QVERIFY2(view, qPrintable(component.errorString()));

        // Sleep has two apps to order, trainings one: only sleep's rows are there.
        QStringList rows;
        for (QQuickItem *i : itemsBelow(qobject_cast<QQuickItem *>(view.get()))) {
            if (QByteArray(i->metaObject()->className()).startsWith("KanteListRow") && i->isVisible()) {
                rows << i->property("text").toString();
            }
        }
        QCOMPARE(rows, QStringList({QStringLiteral("Pebble"), QStringLiteral("Sleep as Android")}));
    }
};

QTEST_MAIN(TestViews)
#include "test_views.moc"
