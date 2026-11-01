#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace kladde {

// One request body for POST /api/measurements/batch with its Idempotency-Key. The key is
// the SHA-256 of the body, so resending the same batch after a lost answer is a replay the
// server recognises (within its 24 h window), and a different batch never collides.
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
