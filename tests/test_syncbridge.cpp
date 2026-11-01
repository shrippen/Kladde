#include "syncbridge.h"
#include "syncbridge_fake.h"

#include <QTest>

// The status the app shows follows a sync under way to its end, without the page being
// opened again (seen on the phone, 1.11.2026: "Health Connect: …Exception" from the run
// before the permissions stayed on screen after the next run had uploaded 3487 entries).
class TestSyncBridge : public QObject
{
    Q_OBJECT

    static constexpr auto Permitted = R"({"available": true, "missing": []})";
    static constexpr auto Running = R"({"running": true, "lastError": "HealthConnectException"})";
    static constexpr auto Done = R"({"running": false, "lastError": null, "uploaded": 3487})";
    static constexpr int Uploaded = 3487;

private Q_SLOTS:
    void init()
    {
        FakeSync::refreshes = 0;
        FakeSync::syncs = 0;
        FakeSync::permissions = Permitted;
    }

    void aRunningSyncIsWatchedUntilItEnds()
    {
        FakeSync::status = Running;
        SyncBridge sync;
        sync.refresh();
        QVERIFY(sync.property("running").toBool());
        QCOMPARE(sync.lastError(), QStringLiteral("HealthConnectException"));

        FakeSync::status = Done;
        QTRY_COMPARE(sync.uploaded(), Uploaded);
        QVERIFY(!sync.property("running").toBool());
        QVERIFY(sync.lastError().isEmpty());

        // Done: no more asking.
        const int asked = FakeSync::refreshes;
        QTest::qWait(3000);
        QCOMPARE(FakeSync::refreshes, asked);
    }

    void syncNowShowsTheRun()
    {
        FakeSync::status = Running;
        SyncBridge sync;
        sync.syncNow();
        QCOMPARE(FakeSync::syncs, 1);
        QVERIFY(FakeSync::refreshes > 0);
        QTRY_VERIFY(sync.property("running").toBool());

        FakeSync::status = Done;
        QTRY_COMPARE(sync.uploaded(), Uploaded);
    }

    void anOptionalKindLeftOutStillSyncs()
    {
        // Weight not allowed: the watch's data still goes, the app offers the rest.
        FakeSync::status = Done;
        FakeSync::permissions = R"({"available": true, "missing": [], "optional": ["android.permission.health.READ_WEIGHT"]})";
        SyncBridge sync;
        sync.refresh();
        QVERIFY(sync.property("permitted").toBool());
        QVERIFY(!sync.property("complete").toBool());

        FakeSync::permissions = Permitted;
        sync.refresh();
        QVERIFY(sync.property("complete").toBool());
    }

    void aNewPermissionReadsTheAppsAgain()
    {
        // Only "Training" allowed of the optional kinds: neither permitted nor complete
        // changes, the training apps must still show (review of #17).
        FakeSync::status = Done;
        FakeSync::permissions = R"({"available": true, "missing": [], "granted": ["READ_SLEEP"], "optional": ["READ_EXERCISE", "READ_WEIGHT"]})";
        SyncBridge sync;
        sync.refresh();
        const int loads = FakeSync::appLoads;

        FakeSync::permissions = R"({"available": true, "missing": [], "granted": ["READ_EXERCISE", "READ_SLEEP"], "optional": ["READ_WEIGHT"]})";
        sync.refresh();
        QCOMPARE(FakeSync::appLoads, loads + 1);

        // The same again: nothing new to read.
        sync.refresh();
        QCOMPARE(FakeSync::appLoads, loads + 1);
    }

    void aMovedAppIsStoredInItsOrder()
    {
        FakeSync::apps = R"({"sleep": [{"app": "coredevices.coreapp", "label": "Pebble"},
                                       {"app": "com.urbandroid.sleep", "label": "Sleep"}],
                             "workouts": [{"app": "com.freeletics.lite", "label": "Freeletics"}]})";
        SyncBridge sync;
        sync.loadApps();
        QCOMPARE(sync.sleepApps().size(), 2);
        QCOMPARE(sync.workoutApps().size(), 1);

        sync.moveApp(SyncBridge::Ranked::Sleep, 1, 0);
        QCOMPARE(FakeSync::orders.value(QStringLiteral("sleep")),
                 QStringList({QStringLiteral("com.urbandroid.sleep"), QStringLiteral("coredevices.coreapp")}));
        QCOMPARE(sync.sleepApps().first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Sleep"));

        // Past the end: nothing moves, nothing stored.
        sync.moveApp(SyncBridge::Ranked::Workouts, 0, 1);
        QVERIFY(!FakeSync::orders.contains(QStringLiteral("workouts")));
    }
};

QTEST_GUILESS_MAIN(TestSyncBridge)
#include "test_syncbridge.moc"
