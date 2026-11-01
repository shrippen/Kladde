#include "api.h"

#include <QJsonArray>

namespace kladde {

namespace {

std::optional<double> number(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    return std::nullopt;
}

QDateTime timestamp(const QJsonValue &v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

QString utc(const QDateTime &t)
{
    return t.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
}

} // namespace

QList<Metric> parseDashboard(const QJsonObject &data)
{
    QList<Metric> out;
    for (const QJsonValue &v : data.value(QLatin1String("metrics")).toArray()) {
        const QJsonObject o = v.toObject();
        Metric m;
        m.kind = o.value(QLatin1String("kind")).toString();
        if (m.kind.isEmpty())
            continue;
        m.latest = number(o.value(QLatin1String("latestValue")));
        m.secondary = number(o.value(QLatin1String("secondaryValue")));
        m.unit = o.value(QLatin1String("unit")).toString();
        m.trend = o.value(QLatin1String("trend")).toString();
        m.sentiment = o.value(QLatin1String("trendSentiment")).toString();
        for (const QJsonValue &p : o.value(QLatin1String("sparkline")).toArray()) {
            // Points are numbers or {value}; gaps (null) are left out.
            if (p.isDouble())
                m.sparkline << p.toDouble();
            else if (p.isObject() && p.toObject().value(QLatin1String("value")).isDouble())
                m.sparkline << p.toObject().value(QLatin1String("value")).toDouble();
        }
        m.updatedAt = timestamp(o.value(QLatin1String("updatedAt")));
        m.allTimeCount = o.value(QLatin1String("allTimeCount")).toInt();
        out << m;
    }
    return out;
}

Series parseSeries(const QJsonObject &data)
{
    Series s;
    s.kind = data.value(QLatin1String("kind")).toString();
    s.unit = data.value(QLatin1String("unit")).toString();
    for (const QJsonValue &v : data.value(QLatin1String("points")).toArray()) {
        const QJsonObject o = v.toObject();
        SeriesPoint p;
        p.at = timestamp(o.value(QLatin1String("at")));
        if (!p.at.isValid() || !o.value(QLatin1String("value")).isDouble())
            continue;
        p.value = o.value(QLatin1String("value")).toDouble();
        p.secondary = number(o.value(QLatin1String("secondary")));
        p.min = number(o.value(QLatin1String("valueMin")));
        p.max = number(o.value(QLatin1String("valueMax")));
        s.points << p;
    }
    std::sort(s.points.begin(), s.points.end(),
              [](const SeriesPoint &a, const SeriesPoint &b) { return a.at < b.at; });

    const QJsonObject stats = data.value(QLatin1String("stats")).toObject();
    s.mean = stats.value(QLatin1String("mean")).toDouble();
    s.min = stats.value(QLatin1String("min")).toDouble();
    s.max = stats.value(QLatin1String("max")).toDouble();
    s.count = stats.value(QLatin1String("count")).toInt();

    const QJsonObject change = data.value(QLatin1String("change")).toObject();
    s.change = number(change.value(QLatin1String("delta")));
    s.changeSentiment = change.value(QLatin1String("sentiment")).toString();

    for (const QJsonValue &v : data.value(QLatin1String("referenceLines")).toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QLatin1String("value")).isDouble())
            continue;
        ReferenceLine r;
        r.value = o.value(QLatin1String("value")).toDouble();
        r.upper = o.value(QLatin1String("kind")).toString() == QLatin1String("upper");
        r.appliesToSecondary = o.value(QLatin1String("appliesTo")).toString() == QLatin1String("secondary");
        r.label = o.value(QLatin1String("label")).toString();
        s.referenceLines << r;
    }
    return s;
}

QString formatValue(const QString &kind, double value, std::optional<double> secondary, const QLocale &locale)
{
    if (kind == QLatin1String("steps"))
        return locale.toString(qRound64(value));
    if (kind == QLatin1String("bloodPressure") && secondary)
        return QStringLiteral("%1/%2").arg(qRound(value)).arg(qRound(*secondary));
    QString text = locale.toString(value, 'f', 1);
    // 72,0 reads as 72.
    const QString zero = locale.decimalPoint() + QLatin1Char('0');
    if (text.endsWith(zero))
        text.chop(zero.size());
    return text;
}

QStringList seriesKinds()
{
    // What the Pebble brings first, then what is typed in.
    return {QStringLiteral("steps"), QStringLiteral("sleep"), QStringLiteral("pulse"),
            QStringLiteral("restingHeartRate"), QStringLiteral("weight"),
            QStringLiteral("bloodPressure"), QStringLiteral("bodyFat")};
}

QList<EntryField> entryFields(EntryKind kind)
{
    // Wide enough for every adult, narrow enough to catch a slip (72 typed as 720).
    switch (kind) {
    case EntryKind::Weight:
        return {{QStringLiteral("value"), 20, 350}};
    case EntryKind::BloodPressure:
        return {{QStringLiteral("systolic"), 60, 260},
                {QStringLiteral("diastolic"), 30, 160},
                {QStringLiteral("pulse"), 25, 250, true}};
    case EntryKind::Pulse:
        return {{QStringLiteral("value"), 25, 250}};
    case EntryKind::BodyFat:
        return {{QStringLiteral("value"), 2, 70}};
    case EntryKind::Mood:
        return {{QStringLiteral("value"), 1, 5}};
    }
    return {};
}

QStringList validateEntry(EntryKind kind, const QHash<QString, double> &values)
{
    QStringList bad;
    for (const EntryField &f : entryFields(kind)) {
        if (!values.contains(f.name)) {
            if (!f.optional)
                bad << f.name;
            continue;
        }
        const double v = values.value(f.name);
        if (!(v >= f.min && v <= f.max))
            bad << f.name;
    }
    if (kind == EntryKind::BloodPressure && !bad.contains(QLatin1String("diastolic"))
        && values.contains(QStringLiteral("systolic")) && values.contains(QStringLiteral("diastolic"))
        && values.value(QStringLiteral("diastolic")) >= values.value(QStringLiteral("systolic")))
        bad << QStringLiteral("diastolic");
    return bad;
}

QList<QJsonObject> measurementBodies(EntryKind kind, const QHash<QString, double> &values,
                                     const QDateTime &at, const QString &note)
{
    auto body = [&](const QString &type, double value) {
        QJsonObject o{{QStringLiteral("type"), type},
                      {QStringLiteral("value"), value},
                      {QStringLiteral("measuredAt"), utc(at)}};
        if (!note.isEmpty())
            o.insert(QStringLiteral("notes"), note);
        return o;
    };
    switch (kind) {
    case EntryKind::Weight:
        return {body(QStringLiteral("WEIGHT"), values.value(QStringLiteral("value")))};
    case EntryKind::BloodPressure: {
        QList<QJsonObject> out{body(QStringLiteral("BLOOD_PRESSURE_SYS"), values.value(QStringLiteral("systolic"))),
                               body(QStringLiteral("BLOOD_PRESSURE_DIA"), values.value(QStringLiteral("diastolic")))};
        if (values.contains(QStringLiteral("pulse")))
            out << body(QStringLiteral("PULSE"), values.value(QStringLiteral("pulse")));
        return out;
    }
    case EntryKind::Pulse:
        return {body(QStringLiteral("PULSE"), values.value(QStringLiteral("value")))};
    case EntryKind::BodyFat:
        return {body(QStringLiteral("BODY_FAT"), values.value(QStringLiteral("value")))};
    case EntryKind::Mood:
        return {};
    }
    return {};
}

QJsonObject moodBody(int mood, const QDateTime &at, const QString &note)
{
    static const QStringList scale{QStringLiteral("LAUSIG"), QStringLiteral("SCHLECHT"),
                                   QStringLiteral("OKAY"), QStringLiteral("GUT"),
                                   QStringLiteral("SUPER_GUT")};
    QJsonObject o{{QStringLiteral("mood"), scale.value(std::clamp(mood, 1, 5) - 1)},
                  {QStringLiteral("moodLoggedAt"), utc(at)}};
    if (!note.isEmpty())
        o.insert(QStringLiteral("note"), note);
    return o;
}

} // namespace kladde
