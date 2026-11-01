#pragma once

#include "session.h"

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QList>

// The demo build's HealthLog: answers the app's calls in-process from the Studio Weber
// world (demo/world.json, key `health`), so screenshots never show real data and need no
// server. Only in builds with KLADDE_DEMO (never in a release).
class DemoBackend
{
public:
    explicit DemoBackend(const QJsonObject &world);

    QString server() const;
    QJsonObject syncStatus() const;
    void answer(const QByteArray &verb, const QString &path, const QByteArray &body,
                const kladde::Session::Callback &done);

    // "@-15m", "@date-1 23:52": the world's times relative to now / today.
    QDateTime when(const QString &text) const;

private:
    struct Point {
        QDateTime at;
        double value = 0;
        std::optional<double> secondary;
    };

    QList<Point> points(const QString &kind, int days) const;
    QJsonObject dashboard() const;
    QJsonObject series(const QString &kind, int days) const;
    QJsonObject tokens() const;
    void store(const QJsonObject &measurement);

    QJsonObject m_health;
    QDate m_today;
    QHash<QString, QList<Point>> m_logged; // typed in during the demo
    QHash<QString, Point> m_pendingPressure; // systolic waiting for its diastolic, by time
    std::optional<int> m_moodToday;
};
