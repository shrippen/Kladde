#pragma once

#include "session.h"

#include <QDateTime>
#include <QObject>
#include <QVariantMap>

// A manual entry (Lastenheft LF-30, LF-33): weight, blood pressure (with pulse), pulse,
// body fat or mood, checked against plausible ranges before anything is sent.
//
// Every measurement gets its own Idempotency-Key, kept until the entry is saved: blood
// pressure is two or three POSTs, and if one fails, "save" again repeats only the request
// the server has not seen (the others come back as replays, nothing doubles).
class Entry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    // The same order as kladde::EntryKind.
    enum class Kind { Weight, BloodPressure, Pulse, BodyFat, Mood };
    Q_ENUM(Kind)

    explicit Entry(kladde::Session *session, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString error() const { return m_error; }

    // The field names (value, systolic, diastolic, pulse) that are missing or out of range.
    Q_INVOKABLE QStringList invalidFields(Kind kind, const QVariantMap &values) const;
    // "20 – 350", for the field's hint.
    Q_INVOKABLE QString range(Kind kind, const QString &field) const;
    Q_INVOKABLE void save(Kind kind, const QVariantMap &values, const QDateTime &at, const QString &note);
    // A new entry: forget the keys of the last one.
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void changed();
    void saved();

private:
    void sendNext();

    kladde::Session *m_session;
    bool m_busy = false;
    QString m_error;
    QByteArray m_signature; // what the keys belong to
    QList<QPair<QString, QJsonObject>> m_requests; // path, body
    QList<QByteArray> m_keys;
    int m_next = 0;
};
