#include "history.h"

#include "api.h"
#include "errors.h"

#include <QJsonObject>
#include <QLocale>
#include <QUrlQuery>

using kladde::ApiReply;

History::History(kladde::Session *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    // Signed out or in again: the last user's series goes, an answer under way for them
    // is dropped (a new request number).
    connect(session, &kladde::Session::generationChanged, this, [this] {
        ++m_request;
        m_loading = false;
        m_error.clear();
        clear();
    });
}

QStringList History::kinds() const
{
    return kladde::seriesKinds();
}

void History::setKind(const QString &kind)
{
    if (kind == m_kind || !kinds().contains(kind))
        return;
    m_kind = kind;
    Q_EMIT kindChanged();
    reload();
}

void History::setDays(int days)
{
    days = std::clamp(days, 1, 3650);
    if (days == m_days)
        return;
    m_days = days;
    Q_EMIT kindChanged();
    reload();
}

void History::clear()
{
    m_series.clear();
    m_labels.clear();
    m_unit.clear();
    m_min = 0;
    m_mean.clear();
    m_low.clear();
    m_high.clear();
    m_change.clear();
    m_changeTrend = 0;
    m_changeGood = 0;
    m_count = 0;
    Q_EMIT changed();
}

void History::reload()
{
    // A newer request wins: switching kinds quickly must not show an older answer.
    const int request = ++m_request;
    m_loading = true;
    Q_EMIT changed();

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("kind"), m_kind);
    query.addQueryItem(QStringLiteral("days"), QString::number(m_days));
    m_session->get(QStringLiteral("/api/measurements/series?") + query.toString(QUrl::FullyEncoded),
                   [this, request, kind = m_kind](const ApiReply &r) {
        if (request != m_request)
            return;
        m_loading = false;
        if (!r.ok()) {
            m_error = Errors::describe(r);
            clear();
            return;
        }
        m_error.clear();
        const kladde::Series s = kladde::parseSeries(r.data().toObject());
        const QLocale locale;
        const QString dateFormat = m_days <= 31 ? QStringLiteral("d.M.") : QStringLiteral("d.M.yy");

        const kladde::SeriesSummary sum = kladde::summarize(kind, s);

        // Blood pressure is drawn as a pair of lines.
        const bool pairs = kind == QLatin1String("bloodPressure");
        QVariantList first;
        QVariantList second;
        m_labels.clear();
        for (const kladde::SeriesPoint &p : sum.points) {
            first << p.value;
            if (pairs) {
                second << *p.secondary;
            }
            m_labels << locale.toString(p.at.toLocalTime(), dateFormat);
        }
        m_series = pairs ? QVariantList{QVariant(first), QVariant(second)} : QVariantList{QVariant(first)};
        m_unit = s.unit;
        m_min = sum.scaleMin;
        m_count = sum.count;
        m_mean = sum.mean;
        m_low = sum.low;
        m_high = sum.high;
        if (s.change && *s.change != 0) {
            m_change = (*s.change > 0 ? QStringLiteral("+") : QString()) + kladde::formatValue(kind, *s.change);
            m_changeTrend = *s.change > 0 ? 1 : -1;
            m_changeGood = kladde::goodDirection(kind, m_changeTrend, s.changeSentiment);
        } else {
            m_change.clear();
            m_changeTrend = 0;
            m_changeGood = kladde::goodDirection(kind, 0, {});
        }
        Q_EMIT changed();
    });
}
