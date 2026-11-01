#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace kladde {

// One request body for POST /api/measurements/batch with its Idempotency-Key. The key is
// new for every batch; resend a batch with its own key after a lost answer and the server
// recognises the replay (24 h window). Not a hash of the body: the server answers a known
// key from memory, so a day total that returns to an earlier value would not be written.
struct Batch {
    QByteArray body;
    QString idempotencyKey;
    qsizetype size = 0;
};

constexpr qsizetype MaxBatchEntries = 500; // HealthLog's limit

// syncTrigger: foreground, background, push, manual or backfill (empty: not sent).
QList<Batch> makeBatches(const QList<QJsonObject> &entries, const QString &syncTrigger = {},
                         qsizetype maxEntries = MaxBatchEntries);

} // namespace kladde
