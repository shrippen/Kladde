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
#include <QQuickWindow>
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

    static constexpr auto Apps = R"({"sleep": [{"app": "coredevices.coreapp", "label": "Pebble"},
                                               {"app": "com.urbandroid.sleep", "label": "Sleep as Android"}],
                                     "workouts": [{"app": "com.freeletics.lite", "label": "Freeletics"}]})";

    enum class Shown { Visible, Hidden };

    // The More page with its C++ objects; Hidden as a tab not shown yet.
    static QQuickItem *more(QQmlEngine &engine, SyncBridge &sync, Account &account, Shown shown = Shown::Visible)
    {
        qmlRegisterUncreatableType<SyncBridge>("Kladde.Core", 1, 0, "SyncBridge", QStringLiteral("test"));
        engine.addImportPath(QStringLiteral(KLADDE_QML));
        engine.rootContext()->setContextProperty(QStringLiteral("sync"), &sync);
        engine.rootContext()->setContextProperty(QStringLiteral("account"), &account);
        QQmlComponent component(&engine, page("MoreView.qml"));
        auto *view = qobject_cast<QQuickItem *>(component.beginCreate(engine.rootContext()));
        if (!view) {
            qWarning() << component.errorString();
            return nullptr;
        }
        view->setVisible(shown == Shown::Visible);
        component.completeCreate();
        return view;
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
        FakeSync::apps = Apps;
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        QQmlEngine engine;
        std::unique_ptr<QQuickItem> view(more(engine, sync, account));
        QVERIFY(view);

        // Sleep has two apps to order, trainings one: only sleep's rows are there.
        QStringList rows;
        for (QQuickItem *i : itemsBelow(view.get())) {
            if (QByteArray(i->metaObject()->className()).startsWith("KanteListRow") && i->isVisible()) {
                rows << i->property("text").toString();
            }
        }
        QCOMPARE(rows, QStringList({QStringLiteral("Pebble"), QStringLiteral("Sleep as Android")}));
    }

    void theArrowsOfARowSitSideBySide()
    {
        // Seen on the phone, 1.11.2026: up and down drawn on top of each other, an "X".
        FakeSync::apps = Apps;
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 2000);
        std::unique_ptr<QQuickItem> view(more(engine, sync, account));
        QVERIFY(view);
        view->setParentItem(window.contentItem());
        view->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QList<qreal> lefts;
        for (QQuickItem *i : itemsBelow(view.get())) {
            if (QByteArray(i->metaObject()->className()).startsWith("KanteToolButton") && i->isVisible()) {
                lefts << i->mapToScene(QPointF()).x();
            }
        }
        QVERIFY(lefts.size() >= 2);
        QVERIFY2(lefts[0] != lefts[1], "up and down on top of each other");
    }

    void moreReadsTheAppsOnlyWhenShown()
    {
        // The tabs create every page at the start; a hidden More reads nothing.
        FakeSync::apps = Apps;
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        QQmlEngine engine;
        QQuickItem tabs;
        const int loads = FakeSync::appLoads;
        std::unique_ptr<QQuickItem> view(more(engine, sync, account, Shown::Hidden));
        QVERIFY(view);
        view->setParentItem(&tabs);
        QCOMPARE(FakeSync::appLoads, loads);

        view->setVisible(true);
        QCOMPARE(FakeSync::appLoads, loads + 1);
    }

    void newAccessShowsTheAppsAgain()
    {
        // Seen on the phone, 1.11.2026: the list stayed empty after allowing Health Connect,
        // until the app was started again.
        FakeSync::apps = Apps;
        FakeSync::permissions = R"({"available": true, "granted": [], "missing": ["android.permission.health.READ_SLEEP"]})";
        MemorySecretStore secrets;
        SyncBridge sync;
        Account account(&secrets, &sync);
        QQmlEngine engine;
        std::unique_ptr<QQuickItem> view(more(engine, sync, account));
        QVERIFY(view);
        const int loads = FakeSync::appLoads;

        FakeSync::permissions = R"({"available": true, "granted": ["android.permission.health.READ_SLEEP"], "missing": []})";
        sync.refresh();
        QCOMPARE(FakeSync::appLoads, loads + 1);
    }
};

QTEST_MAIN(TestViews)
#include "test_views.moc"
