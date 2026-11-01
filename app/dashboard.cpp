#include "dashboard.h"

#include "api.h"
#include "errors.h"

#include <QJsonObject>

using kladde::ApiReply;

Dashboard::Dashboard(kladde::Session *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    // Signed out or in again: nothing of the last user stays on screen, and an answer still
    // under way for them is dropped (reload checks the generation).
    connect(session, &kladde::Session::generationChanged, this, [this] {
        m_metrics.clear();
        m_error.clear();
        m_loading = false;
        Q_EMIT changed();
    });
}

void Dashboard::reload()
{
    if (m_loading)
        return;
    m_loading = true;
    Q_EMIT changed();
    m_session->get(QStringLiteral("/api/dashboard/summary"), [this, g = m_session->generation()](const ApiReply &r) {
        if (g != m_session->generation())
            return;
        m_loading = false;
        if (!r.ok()) {
            m_error = Errors::describe(r);
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
