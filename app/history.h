#pragma once

#include "session.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

// The history of one metric over a range (Lastenheft LF-32), from
// GET /api/measurements/series, shaped for KanteLineChart: `series` holds one list of
// values per line (blood pressure: systolic and diastolic), `labels` the dates.
class History : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList kinds READ kinds CONSTANT)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    Q_PROPERTY(int days READ days WRITE setDays NOTIFY kindChanged)
    Q_PROPERTY(QVariantList series READ series NOTIFY changed)
    Q_PROPERTY(QStringList labels READ labels NOTIFY changed)
    Q_PROPERTY(QString unit READ unit NOTIFY changed)
    Q_PROPERTY(double minValue READ minValue NOTIFY changed)
    Q_PROPERTY(QString mean READ mean NOTIFY changed)
    Q_PROPERTY(QString low READ low NOTIFY changed)
    Q_PROPERTY(QString high READ high NOTIFY changed)
    Q_PROPERTY(QString change READ change NOTIFY changed)
    Q_PROPERTY(int changeTrend READ changeTrend NOTIFY changed)
    Q_PROPERTY(int changeGood READ changeGood NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit History(kladde::Session *session, QObject *parent = nullptr);

    QStringList kinds() const;
    QString kind() const { return m_kind; }
    void setKind(const QString &kind);
    int days() const { return m_days; }
    void setDays(int days);

    QVariantList series() const { return m_series; }
    QStringList labels() const { return m_labels; }
    QString unit() const { return m_unit; }
    double minValue() const { return m_min; }
    QString mean() const { return m_mean; }
    QString low() const { return m_low; }
    QString high() const { return m_high; }
    QString change() const { return m_change; }
    int changeTrend() const { return m_changeTrend; }
    int changeGood() const { return m_changeGood; }
    int count() const { return m_count; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }

    Q_INVOKABLE void reload();

Q_SIGNALS:
    void kindChanged();
    void changed();

private:
    void clear();

    kladde::Session *m_session;
    QString m_kind = QStringLiteral("steps");
    int m_days = 30;
    int m_request = 0;
    QVariantList m_series;
    QStringList m_labels;
    QString m_unit;
    double m_min = 0;
    QString m_mean;
    QString m_low;
    QString m_high;
    QString m_change;
    int m_changeTrend = 0;
    int m_changeGood = 0;
    int m_count = 0;
    bool m_loading = false;
    QString m_error;
};
