#include "serialqueue.h"

#include <QSemaphore>
#include <QTest>
#include <QThread>

#include <atomic>

// The vault's calls on Android go through this queue (app/sessionvault_android.cpp).
class TestSerialQueue : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void doesNotBlockTheCaller()
    {
        // A renewal of the background job holds the vault's lock: the UI thread must not wait
        // for it (Android kills an app that stops answering).
        SerialQueue queue;
        QSemaphore renewal;
        QThread *answeredOn = nullptr;
        queue.run([&] { return renewal.tryAcquire(1, 5000); }, [&](bool) { answeredOn = QThread::currentThread(); });
        QVERIFY(answeredOn == nullptr);

        renewal.release();
        QTRY_VERIFY(answeredOn != nullptr);
        QCOMPARE(answeredOn, QThread::currentThread());
    }

    void oneAtATimeInOrder()
    {
        // Sign-out reads the kept token, then removes it: never the other way round.
        SerialQueue queue;
        std::atomic<int> running = 0;
        std::atomic<int> most = 0;
        QList<int> answers;
        for (int i = 0; i < 3; ++i) {
            queue.run([&, i] {
                most = std::max(most.load(), ++running);
                QThread::msleep(30 - 10 * i);
                --running;
                return i;
            }, [&](int r) { answers << r; });
        }
        QTRY_COMPARE(answers.size(), 3);
        QCOMPARE(answers, (QList<int>{0, 1, 2}));
        QCOMPARE(most.load(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSerialQueue)
#include "test_serialqueue.moc"
