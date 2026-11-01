#include "dashboard.h"

#include "api.h"

#include <QJsonObject>

using kladde::ApiReply;

Dashboard::Dashboard(kladde::Session *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    connect(session, &kladde::Session::stateChanged, this, [this] {
        if (m_session->state() == kladde::Session::State::SignedOut && !m_metrics.isEmpty()) {
            m_metrics.clear(); // nothing of the last user stays on screen
            Q_EMIT changed();
        }
    });
}

void Dashboard::reload()
{
    if (m_loading)
        return;
    m_loading = true;
    Q_EMIT changed();
    m_session->get(QStringLiteral("/api/dashboard/summary"), [this](const ApiReply &r) {
        m_loading = false;
        if (!r.ok()) {
            m_error = r.error();
            Q_EMIT changed();
            return;
        }
        m_error.clear();
        m_metrics.clear();
        for (const kladde::Metric &m : kladde::parseDashboard(r.data().toObject())) {
            if (!m.latest)
                continue; // never measured: no empty tiles
            QVariantList spark;
            for (double v : m.sparkline)
                spark << v;
            const int trend = m.trend == QLatin1String("up") ? 1 : m.trend == QLatin1String("down") ? -1 : 0;
            // The server says whether the move is good; turn that into Kante's "which way is good".
            const int good = m.sentiment == QLatin1String("positive") ? trend
                           : m.sentiment == QLatin1String("negative") ? -trend : 0;
            m_metrics << QVariantMap{
                {QStringLiteral("kind"), m.kind},
                {QStringLiteral("value"), kladde::formatValue(m.kind, *m.latest, m.secondary)},
                {QStringLiteral("unit"), m.unit},
                {QStringLiteral("trend"), trend},
                {QStringLiteral("good"), good},
                {QStringLiteral("sparkline"), spark},
                {QStringLiteral("updatedAt"), m.updatedAt},
            };
        }
        Q_EMIT changed();
    });
}
