#include "history.h"

#include "api.h"
#include "errors.h"

#include <QJsonObject>
#include <QLocale>
#include <QUrlQuery>

using kladde::ApiReply;

namespace {

// Totals start at zero; levels (weight, pulse, blood pressure) show their own band.
bool isTotal(const QString &kind)
{
    return kind == QLatin1String("steps") || kind == QLatin1String("sleep");
}

} // namespace

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

        // Blood pressure is a pair: a point without its diastolic value is left out, not
        // drawn with the systolic one in its place.
        const bool pairs = kind == QLatin1String("bloodPressure");
        QList<kladde::SeriesPoint> shown;
        for (const kladde::SeriesPoint &p : s.points) {
            if (!pairs || p.secondary)
                shown << p;
        }

        QVariantList first;
        QVariantList second;
        m_labels.clear();
        double sumSecondary = 0;
        const kladde::SeriesPoint *lowest = nullptr;
        const kladde::SeriesPoint *highest = nullptr;
        for (const kladde::SeriesPoint &p : shown) {
            first << p.value;
            if (pairs) {
                second << *p.secondary;
                sumSecondary += *p.secondary;
            }
            if (!lowest || p.value < lowest->value)
                lowest = &p;
            if (!highest || p.value > highest->value)
                highest = &p;
            m_labels << locale.toString(p.at.toLocalTime(), dateFormat);
        }
        m_series = pairs ? QVariantList{QVariant(first), QVariant(second)} : QVariantList{QVariant(first)};
        m_unit = s.unit;

        double low = lowest ? lowest->value : 0;
        for (const kladde::SeriesPoint &p : shown) {
            if (p.secondary)
                low = std::min(low, *p.secondary);
        }
        m_min = isTotal(kind) || shown.isEmpty() ? 0 : std::floor(low * 0.9);

        m_count = pairs ? int(shown.size()) : s.count;
        if (pairs && !shown.isEmpty()) {
            // The pair everywhere: average of both, the readings with the lowest and highest
            // systolic value.
            double sumFirst = 0;
            for (const kladde::SeriesPoint &p : shown)
                sumFirst += p.value;
            m_mean = kladde::formatValue(kind, sumFirst / shown.size(), sumSecondary / shown.size());
            m_low = kladde::formatValue(kind, lowest->value, lowest->secondary);
            m_high = kladde::formatValue(kind, highest->value, highest->secondary);
        } else {
            m_mean = s.count ? kladde::formatValue(kind, s.mean) : QString();
            m_low = s.count ? kladde::formatValue(kind, s.min) : QString();
            m_high = s.count ? kladde::formatValue(kind, s.max) : QString();
        }
        if (s.change && *s.change != 0) {
            m_change = (*s.change > 0 ? QStringLiteral("+") : QString()) + kladde::formatValue(kind, *s.change);
            m_changeTrend = *s.change > 0 ? 1 : -1;
            m_changeGood = s.changeSentiment == QLatin1String("positive") ? m_changeTrend
                         : s.changeSentiment == QLatin1String("negative") ? -m_changeTrend : 0;
        } else {
            m_change.clear();
            m_changeTrend = 0;
            m_changeGood = 0;
        }
        Q_EMIT changed();
    });
}
