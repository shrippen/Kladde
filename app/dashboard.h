#pragma once

#include "session.h"

#include <QObject>
#include <QVariantList>

// Home (Lastenheft LF-31): the latest value of every metric with its last seven days,
// from GET /api/dashboard/summary. A tile is a map: kind, value (formatted), unit, trend
// (1 up, -1 down, 0 flat), good (which way is good: 1, -1, 0), sparkline, updatedAt.
class Dashboard : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList metrics READ metrics NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit Dashboard(kladde::Session *session, QObject *parent = nullptr);

    QVariantList metrics() const { return m_metrics; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }

    Q_INVOKABLE void reload();

Q_SIGNALS:
    void changed();

private:
    kladde::Session *m_session;
    QVariantList m_metrics;
    bool m_loading = false;
    QString m_error;
};
