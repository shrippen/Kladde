#include "demobackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrlQuery>

#include <cmath>

using kladde::ApiReply;

namespace {

// The world's daily series and their cycle: values repeat backwards for longer ranges.
const QHash<QString, QString> DailyKey{
    {QStringLiteral("steps"), QStringLiteral("steps")},
    {QStringLiteral("sleep"), QStringLiteral("sleep_minutes")},
    {QStringLiteral("restingHeartRate"), QStringLiteral("resting_heart_rate")},
    {QStringLiteral("pulse"), QStringLiteral("pulse")},
    {QStringLiteral("mood"), QStringLiteral("mood")},
};
const QHash<QString, QString> ReadingKey{
    {QStringLiteral("weight"), QStringLiteral("weight")},
    {QStringLiteral("bloodPressure"), QStringLiteral("blood_pressure")},
    {QStringLiteral("bodyFat"), QStringLiteral("body_fat")},
};
// HealthLog's units for these kinds (API constants, not demo data).
const QHash<QString, QString> Units{
    {QStringLiteral("steps"), QStringLiteral("steps")},
    {QStringLiteral("sleep"), QStringLiteral("h")},
    {QStringLiteral("restingHeartRate"), QStringLiteral("bpm")},
    {QStringLiteral("pulse"), QStringLiteral("bpm")},
    {QStringLiteral("weight"), QStringLiteral("kg")},
    {QStringLiteral("bloodPressure"), QStringLiteral("mmHg")},
    {QStringLiteral("bodyFat"), QStringLiteral("%")},
    {QStringLiteral("mood"), QString()},
};
const QHash<QString, QString> TypeKind{
    {QStringLiteral("WEIGHT"), QStringLiteral("weight")},
    {QStringLiteral("PULSE"), QStringLiteral("pulse")},
    {QStringLiteral("BODY_FAT"), QStringLiteral("bodyFat")},
};
const QStringList MoodScale{QStringLiteral("LAUSIG"), QStringLiteral("SCHLECHT"), QStringLiteral("OKAY"),
                            QStringLiteral("GUT"), QStringLiteral("SUPER_GUT")};

QString iso(const QDateTime &t)
{
    return t.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
}

ApiReply ok(const QJsonValue &data)
{
    ApiReply r;
    r.status = 200;
    r.root = QJsonObject{{QStringLiteral("data"), data}, {QStringLiteral("error"), QJsonValue()}};
    return r;
}

} // namespace

DemoBackend::DemoBackend(const QJsonObject &world)
    : m_health(world.value(QLatin1String("health")).toObject())
{
    const QByteArray fixed = qgetenv("DEMO_TODAY");
    m_today = fixed.isEmpty() ? QDate::currentDate() : QDate::fromString(QString::fromLatin1(fixed), Qt::ISODate);
}

QString DemoBackend::server() const
{
    return m_health.value(QLatin1String("account")).toObject().value(QLatin1String("server")).toString();
}

QDateTime DemoBackend::when(const QString &text) const
{
    static const QRegularExpression day(QStringLiteral(R"(^@date([+-]\d+) (\d\d):(\d\d)$)"));
    static const QRegularExpression ago(QStringLiteral(R"(^@([+-])(?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?$)"));
    if (const auto m = day.match(text); m.hasMatch())
        return QDateTime(m_today.addDays(m.captured(1).toInt()), QTime(m.captured(2).toInt(), m.captured(3).toInt()));
    if (const auto m = ago.match(text); m.hasMatch()) {
        const qint64 secs = m.captured(2).toLongLong() * 86400 + m.captured(3).toLongLong() * 3600
            + m.captured(4).toLongLong() * 60;
        const QDateTime now(m_today, QTime::currentTime());
        return now.addSecs(m.captured(1) == QLatin1String("-") ? -secs : secs);
    }
    return {};
}

QJsonObject DemoBackend::syncStatus() const
{
    const QJsonObject s = m_health.value(QLatin1String("sync")).toObject();
    return QJsonObject{{QStringLiteral("lastRun"), iso(when(s.value(QLatin1String("last_run")).toString()))},
                       {QStringLiteral("lastSuccess"), iso(when(s.value(QLatin1String("last_success")).toString()))},
                       {QStringLiteral("uploaded"), s.value(QLatin1String("uploaded"))},
                       {QStringLiteral("pending"), s.value(QLatin1String("pending"))}};
}

QList<DemoBackend::Point> DemoBackend::points(const QString &kind, int days) const
{
    QList<Point> out;
    const QDate first = m_today.addDays(1 - days);
    if (DailyKey.contains(kind)) {
        const QJsonArray values = m_health.value(QLatin1String("daily")).toObject().value(DailyKey.value(kind)).toArray();
        const qsizetype n = values.size();
        for (int back = days - 1; back >= 0 && n > 0; --back) {
            double v = values.at(n - 1 - back % n).toDouble();
            if (kind == QLatin1String("sleep"))
                v = std::round(v / 6.0) / 10.0; // minutes to hours, one decimal
            if (kind == QLatin1String("mood") && back == 0 && m_moodToday)
                v = *m_moodToday;
            out << Point{QDateTime(m_today.addDays(-back), QTime(12, 0)), v, {}};
        }
    } else if (ReadingKey.contains(kind)) {
        const QJsonArray readings = m_health.value(QLatin1String("readings")).toObject().value(ReadingKey.value(kind)).toArray();
        // The readings cover four weeks; earlier ranges repeat them.
        constexpr int Cycle = 28;
        for (int shift = 0; -shift * Cycle + Cycle > -days; ++shift) {
            for (const QJsonValue &v : readings) {
                const QJsonObject r = v.toObject();
                const QDate d = m_today.addDays(r.value(QLatin1String("day")).toInt() - shift * Cycle);
                if (d < first)
                    continue;
                const QTime t = QTime::fromString(r.value(QLatin1String("time")).toString(), QStringLiteral("HH:mm"));
                Point p{QDateTime(d, t), r.value(QLatin1String(kind == QLatin1String("bloodPressure") ? "systolic" : "value")).toDouble(), {}};
                if (kind == QLatin1String("bloodPressure"))
                    p.secondary = r.value(QLatin1String("diastolic")).toDouble();
                out << p;
            }
        }
    }
    for (const Point &p : m_logged.value(kind)) {
        if (p.at.date() >= first)
            out << p;
    }
    std::sort(out.begin(), out.end(), [](const Point &a, const Point &b) { return a.at < b.at; });
    return out;
}

QJsonObject DemoBackend::series(const QString &kind, int days) const
{
    const QList<Point> pts = points(kind, days);
    QJsonArray array;
    double sum = 0;
    double low = pts.isEmpty() ? 0 : pts.first().value;
    double high = low;
    for (const Point &p : pts) {
        array << QJsonObject{{QStringLiteral("id"), iso(p.at)},
                             {QStringLiteral("at"), iso(p.at)},
                             {QStringLiteral("value"), p.value},
                             {QStringLiteral("secondary"), p.secondary ? QJsonValue(*p.secondary) : QJsonValue()}};
        sum += p.value;
        low = std::min(low, p.value);
        high = std::max(high, p.value);
    }
    const qsizetype n = pts.size();
    QJsonObject data{{QStringLiteral("kind"), kind},
                     {QStringLiteral("unit"), Units.value(kind)},
                     {QStringLiteral("points"), array},
                     {QStringLiteral("stats"), QJsonObject{{QStringLiteral("mean"), n ? std::round(sum / n * 10) / 10 : 0},
                                                           {QStringLiteral("min"), low},
                                                           {QStringLiteral("max"), high},
                                                           {QStringLiteral("stdDev"), 0},
                                                           {QStringLiteral("count"), int(n)}}}};
    if (n > 1) {
        const double delta = std::round((pts.last().value - pts.first().value) * 10) / 10;
        data.insert(QStringLiteral("change"), QJsonObject{{QStringLiteral("from"), pts.first().value},
                                                          {QStringLiteral("to"), pts.last().value},
                                                          {QStringLiteral("delta"), delta},
                                                          {QStringLiteral("sentiment"), QStringLiteral("neutral")}});
    }
    return data;
}

QJsonObject DemoBackend::dashboard() const
{
    QJsonArray metrics;
    const QStringList kinds{QStringLiteral("steps"), QStringLiteral("sleep"), QStringLiteral("pulse"),
                            QStringLiteral("weight"), QStringLiteral("bloodPressure"), QStringLiteral("bodyFat"),
                            QStringLiteral("mood")};
    for (const QString &kind : kinds) {
        // The last seven values (days for daily kinds, readings otherwise).
        QList<Point> pts = points(kind, DailyKey.contains(kind) ? 7 : 60);
        if (pts.isEmpty())
            continue;
        if (pts.size() > 7)
            pts = pts.mid(pts.size() - 7);
        QJsonArray spark;
        for (const Point &p : pts)
            spark << p.value;
        const Point &last = pts.last();
        const double before = pts.size() > 1 ? pts.at(pts.size() - 2).value : last.value;
        const QString trend = last.value > before ? QStringLiteral("up")
                            : last.value < before ? QStringLiteral("down") : QStringLiteral("flat");
        metrics << QJsonObject{{QStringLiteral("id"), kind},
                               {QStringLiteral("kind"), kind},
                               {QStringLiteral("titleKey"), QStringLiteral("dashboard.metric.title.") + kind},
                               {QStringLiteral("latestValue"), last.value},
                               {QStringLiteral("secondaryValue"), last.secondary ? QJsonValue(*last.secondary) : QJsonValue()},
                               {QStringLiteral("unit"), Units.value(kind)},
                               {QStringLiteral("trend"), trend},
                               {QStringLiteral("sparkline"), spark},
                               {QStringLiteral("updatedAt"), iso(last.at)},
                               {QStringLiteral("allTimeCount"), int(pts.size())}};
    }
    return QJsonObject{{QStringLiteral("metrics"), metrics}};
}

QJsonObject DemoBackend::tokens() const
{
    const QJsonObject account = m_health.value(QLatin1String("account")).toObject();
    return QJsonObject{{QStringLiteral("user"), QJsonObject{{QStringLiteral("id"), QStringLiteral("demo")},
                                                           {QStringLiteral("username"), account.value(QLatin1String("username"))}}},
                       {QStringLiteral("token"), QStringLiteral("demo-access")},
                       {QStringLiteral("tokenExpiresAt"), iso(QDateTime::currentDateTimeUtc().addDays(1))},
                       {QStringLiteral("refreshToken"), QStringLiteral("demo-refresh")},
                       {QStringLiteral("refreshTokenExpiresAt"), iso(QDateTime::currentDateTimeUtc().addDays(30))}};
}

void DemoBackend::store(const QJsonObject &m)
{
    const QString type = m.value(QLatin1String("type")).toString();
    const QDateTime at = QDateTime::fromString(m.value(QLatin1String("measuredAt")).toString(), Qt::ISODateWithMs).toLocalTime();
    const double value = m.value(QLatin1String("value")).toDouble();
    const QString stamp = iso(at);
    if (type == QLatin1String("BLOOD_PRESSURE_SYS")) {
        m_pendingPressure.insert(stamp, Point{at, value, {}});
    } else if (type == QLatin1String("BLOOD_PRESSURE_DIA")) {
        Point p = m_pendingPressure.take(stamp);
        p.at = at;
        p.secondary = value;
        m_logged[QStringLiteral("bloodPressure")] << p;
    } else if (TypeKind.contains(type)) {
        m_logged[TypeKind.value(type)] << Point{at, value, {}};
    }
}

void DemoBackend::answer(const QByteArray &verb, const QString &path, const QByteArray &body,
                         const kladde::Session::Callback &done)
{
    const QUrl url(path);
    const QString route = url.path();
    const QJsonObject request = QJsonDocument::fromJson(body).object();

    if (route == QLatin1String("/api/version")) {
        done(ok(QJsonObject{{QStringLiteral("version"),
                             m_health.value(QLatin1String("account")).toObject().value(QLatin1String("version"))}}));
    } else if (route == QLatin1String("/api/auth/login") || route == QLatin1String("/api/auth/refresh")) {
        done(ok(request.value(QLatin1String("revoke")).toBool() ? QJsonObject{{QStringLiteral("revoked"), true}} : tokens()));
    } else if (route == QLatin1String("/api/auth/logout")) {
        done(ok(QJsonObject{{QStringLiteral("loggedOut"), true}}));
    } else if (route == QLatin1String("/api/tokens/measurements")) {
        done(ok(QJsonObject{{QStringLiteral("token"), QStringLiteral("demo-sync")}, {QStringLiteral("name"), request.value(QLatin1String("name"))}}));
    } else if (route == QLatin1String("/api/dashboard/summary")) {
        done(ok(dashboard()));
    } else if (route == QLatin1String("/api/measurements/series")) {
        const QUrlQuery q(url);
        done(ok(series(q.queryItemValue(QStringLiteral("kind")), q.queryItemValue(QStringLiteral("days")).toInt())));
    } else if (verb == "POST" && route == QLatin1String("/api/measurements")) {
        store(request);
        done(ok(request));
    } else if (verb == "POST" && route == QLatin1String("/api/mood-entries")) {
        m_moodToday = MoodScale.indexOf(request.value(QLatin1String("mood")).toString()) + 1;
        done(ok(request));
    } else {
        ApiReply r;
        r.status = 404;
        done(r);
    }
}
