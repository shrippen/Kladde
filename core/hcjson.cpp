#include "hcjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace kladde {

namespace {

// Health Connect stage types (SleepSessionRecord.STAGE_TYPE_*).
constexpr int StageUnknown = 0;
constexpr int StageOutOfBed = 3;

QDateTime readTime(const QJsonObject &o, const char *key)
{
    QDateTime t = QDateTime::fromString(o.value(QLatin1String(key)).toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toUTC() : QDateTime();
}

} // namespace

ParseResult parseHealthConnectJson(const QByteArray &json)
{
    ParseResult result;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = parseError.errorString();
        return result;
    }
    if (!doc.object().value(QLatin1String("records")).isArray()) {
        result.error = QStringLiteral("no \"records\" array");
        return result;
    }

    const QJsonArray records = doc.object().value(QLatin1String("records")).toArray();
    for (qsizetype i = 0; i < records.size(); ++i) {
        const QJsonObject r = records.at(i).toObject();
        const QString type = r.value(QLatin1String("type")).toString();
        Sample base;
        base.id = r.value(QLatin1String("id")).toString();
        base.sourceApp = r.value(QLatin1String("app")).toString();
        auto skip = [&](const QString &why) {
            result.warnings << QStringLiteral("record %1 (%2): %3").arg(i).arg(type, why);
        };
        if (base.id.isEmpty()) {
            skip(QStringLiteral("no id"));
            continue;
        }

        if (type == QLatin1String("Steps") || type == QLatin1String("Distance")) {
            const bool steps = type == QLatin1String("Steps");
            Sample s = base;
            s.kind = steps ? Sample::Kind::Steps : Sample::Kind::Distance;
            s.start = readTime(r, "start");
            s.end = readTime(r, "end");
            s.value = r.value(QLatin1String(steps ? "count" : "meters")).toDouble(-1);
            if (!s.start.isValid() || !s.end.isValid() || s.value < 0) {
                skip(QStringLiteral("bad interval or value"));
                continue;
            }
            result.samples << s;
        } else if (type == QLatin1String("HeartRate")) {
            const QJsonArray points = r.value(QLatin1String("samples")).toArray();
            for (const QJsonValue &p : points) {
                Sample s = base;
                s.kind = Sample::Kind::HeartRate;
                s.start = s.end = readTime(p.toObject(), "time");
                s.value = p.toObject().value(QLatin1String("bpm")).toDouble(-1);
                if (!s.start.isValid() || s.value <= 0) {
                    skip(QStringLiteral("bad heart rate sample"));
                    continue;
                }
                result.samples << s;
            }
        } else if (type == QLatin1String("RestingHeartRate")) {
            Sample s = base;
            s.kind = Sample::Kind::RestingHeartRate;
            s.start = s.end = readTime(r, "time");
            s.value = r.value(QLatin1String("bpm")).toDouble(-1);
            if (!s.start.isValid() || s.value <= 0) {
                skip(QStringLiteral("bad resting heart rate"));
                continue;
            }
            result.samples << s;
        } else if (type == QLatin1String("SleepSession")) {
            const QJsonArray stages = r.value(QLatin1String("stages")).toArray();
            bool known = false;
            for (const QJsonValue &st : stages) {
                const int stage = st.toObject().value(QLatin1String("stage")).toInt();
                known = known || (stage != StageUnknown && stage != StageOutOfBed);
            }
            auto addStage = [&](const QDateTime &start, const QDateTime &end, int stage) {
                Sample s = base;
                s.kind = Sample::Kind::SleepStage;
                s.start = start;
                s.end = end;
                s.sleepStage = stage;
                s.sessionHasKnownStages = known;
                if (!start.isValid() || !end.isValid() || end <= start) {
                    skip(QStringLiteral("bad sleep stage interval"));
                    return;
                }
                result.samples << s;
            };
            if (stages.isEmpty()) {
                // A session without stages is one block of plain sleep, as in HealthLog's
                // own Health Connect import.
                addStage(readTime(r, "start"), readTime(r, "end"), StageUnknown);
            }
            for (const QJsonValue &st : stages) {
                const QJsonObject o = st.toObject();
                addStage(readTime(o, "start"), readTime(o, "end"),
                         o.value(QLatin1String("stage")).toInt());
            }
        } else {
            skip(QStringLiteral("type not mapped"));
        }
    }
    return result;
}

} // namespace kladde
