#include "batch.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

namespace kladde {

QList<Batch> makeBatches(const QList<QJsonObject> &entries, const QString &syncTrigger,
                         qsizetype maxEntries)
{
    QList<Batch> batches;
    for (qsizetype from = 0; from < entries.size(); from += maxEntries) {
        QJsonArray array;
        const qsizetype to = std::min(from + maxEntries, entries.size());
        for (qsizetype i = from; i < to; ++i)
            array.append(entries.at(i));
        QJsonObject root{{QStringLiteral("entries"), array}};
        if (!syncTrigger.isEmpty())
            root.insert(QStringLiteral("syncTrigger"), syncTrigger);

        Batch b;
        b.body = QJsonDocument(root).toJson(QJsonDocument::Compact);
        b.idempotencyKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
        b.size = to - from;
        batches << b;
    }
    return batches;
}

} // namespace kladde
