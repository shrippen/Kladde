#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace kladde {
Q_NAMESPACE

// One tile of GET /api/dashboard/summary.
struct Metric {
    QString kind; // weight, bloodPressure, pulse, sleep, steps, mood …
    std::optional<double> latest;
    std::optional<double> secondary; // diastolic for blood pressure
    QString unit;
    QString trend; // up, down, flat, unknown
    QString sentiment; // positive, negative, neutral, or empty
    QList<double> sparkline;
    QDateTime updatedAt;
    int allTimeCount = 0;
};

QList<Metric> parseDashboard(const QJsonObject &data);

// GET /api/measurements/series?kind=…&days=…
struct SeriesPoint {
    QDateTime at;
    double value = 0;
    std::optional<double> secondary;
    std::optional<double> min;
    std::optional<double> max;
};

struct ReferenceLine {
    double value = 0;
    bool upper = false;
    bool appliesToSecondary = false;
    QString label;
};

struct Series {
    QString kind;
    QString unit;
    QList<SeriesPoint> points; // oldest first
    double mean = 0;
    double min = 0;
    double max = 0;
    int count = 0;
    std::optional<double> change; // delta over the range
    QString changeSentiment;
    QList<ReferenceLine> referenceLines;
};

Series parseSeries(const QJsonObject &data);

// A metric's value as the app shows it, without the unit: steps whole and grouped
// ("8.412"), blood pressure "128/82", everything else with at most one decimal.
QString formatValue(const QString &kind, double value, std::optional<double> secondary = {},
                    const QLocale &locale = QLocale());

// Which way is good for a metric: 1 up, -1 down, 0 neither. The server's verdict on the
// move (`trend` 1 up, -1 down; `sentiment` positive, negative) wins; without one the
// kind decides (steps: more is good, pulse: less is good).
int goodDirection(const QString &kind, int trend, const QString &sentiment);

// What the history shows of a series: the points drawn, their count, mean, lowest and
// highest value as text, and the lowest mark of the scale. Blood pressure is a pair: a
// point without its diastolic value is left out, and mean, lowest and highest are
// pairs ("128/82"); lowest and highest are the readings with the lowest and highest
// systolic value.
struct SeriesSummary {
    QList<SeriesPoint> points;
    int count = 0;
    QString mean;
    QString low;
    QString high;
    double scaleMin = 0; // totals start at zero, levels at 90 % of their lowest value
};

SeriesSummary summarize(const QString &kind, const Series &series);

// The kinds the history shows: what the Pebble brings first, then what is typed in.
QStringList seriesKinds();

// A manual entry (Lastenheft LF-30 … LF-33). One form kind can mean several measurements:
// blood pressure is systolic and diastolic, sent as two POSTs, plus the pulse if given.
enum class EntryKind { Weight, BloodPressure, Pulse, BodyFat, Mood };
Q_ENUM_NS(EntryKind)

struct EntryField {
    QString name; // value, systolic, diastolic, pulse
    double min = 0;
    double max = 0;
    bool optional = false;
};

QList<EntryField> entryFields(EntryKind kind);

// Values keyed by field name. Empty when everything is in range, else the field names out
// of range or missing (diastolic above systolic counts as out of range).
QStringList validateEntry(EntryKind kind, const QHash<QString, double> &values);

// The bodies for POST /api/measurements, one per measurement, measuredAt in UTC.
QList<QJsonObject> measurementBodies(EntryKind kind, const QHash<QString, double> &values,
                                     const QDateTime &at, const QString &note = {});

// mood 1 (lausig) … 5 (super gut), as HealthLog's scale.
QJsonObject moodBody(int mood, const QDateTime &at, const QString &note = {});

} // namespace kladde
