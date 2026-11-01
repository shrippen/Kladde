#pragma once

#include <QObject>
#include <QPointer>
#include <QThreadPool>

// Runs blocking calls one after another off the caller's thread; each answer comes back on
// this object's thread, in the order of the calls. For the vault on Android: a JNI call may
// wait for a renewal of the background job (up to a minute), never on the UI thread.
class SerialQueue : public QObject
{
    Q_OBJECT
public:
    explicit SerialQueue(QObject *parent = nullptr);

    // `job` runs on the queue's thread, `done(result)` on ours.
    template <typename Job, typename Done>
    void run(Job job, Done done)
    {
        QPointer<SerialQueue> self(this);
        m_thread.start([self, job = std::move(job), done = std::move(done)]() mutable {
            auto result = job();
            if (!self) {
                return;
            }
            // Posted events to one object arrive in order: the answers keep the calls' order.
            QMetaObject::invokeMethod(self, [done = std::move(done), result = std::move(result)]() mutable {
                done(std::move(result));
            }, Qt::QueuedConnection);
        });
    }

private:
    // One thread: the calls never overlap and run in order.
    QThreadPool m_thread;
};
