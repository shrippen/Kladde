#include "entry.h"

#include "api.h"
#include "errors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QUuid>

using kladde::ApiReply;

namespace {

kladde::EntryKind core(Entry::Kind kind)
{
    return static_cast<kladde::EntryKind>(kind);
}

QHash<QString, double> numbers(const QVariantMap &values)
{
    QHash<QString, double> out;
    for (auto it = values.begin(); it != values.end(); ++it) {
        bool ok = false;
        // Typed text with either decimal mark (72,4 or 72.4).
        const double v = it.value().toString().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (ok)
            out.insert(it.key(), v);
    }
    return out;
}

} // namespace

Entry::Entry(kladde::Session *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    // Signed out or in again: a save under way belongs to the old session (its answer is
    // dropped in send), and its Idempotency-Key is not reused for another account.
    connect(session, &kladde::Session::generationChanged, this, [this] {
        m_busy = false;
        m_error.clear();
        m_signature.clear();
        m_key.clear();
        Q_EMIT changed();
    });
}

QStringList Entry::invalidFields(Kind kind, const QVariantMap &values) const
{
    return kladde::validateEntry(core(kind), numbers(values));
}

QString Entry::range(Kind kind, const QString &field) const
{
    const QLocale locale;
    for (const kladde::EntryField &f : kladde::entryFields(core(kind))) {
        if (f.name == field)
            return QStringLiteral("%1 – %2").arg(locale.toString(f.min), locale.toString(f.max));
    }
    return {};
}

void Entry::reset()
{
    m_signature.clear();
    m_key.clear();
    m_error.clear();
    Q_EMIT changed();
}

void Entry::save(Kind kind, const QVariantMap &values, const QDateTime &at, const QString &note)
{
    if (m_busy)
        return;
    const QHash<QString, double> v = numbers(values);
    if (!kladde::validateEntry(core(kind), v).isEmpty())
        return;

    if (kind == Kind::Mood) {
        m_path = QStringLiteral("/api/mood-entries");
        m_body = kladde::moodBody(qRound(v.value(QStringLiteral("value"))), at, note.trimmed());
    } else {
        m_path = QStringLiteral("/api/measurements");
        const QList<QJsonObject> bodies = kladde::measurementBodies(core(kind), v, at, note.trimmed());
        if (bodies.size() == 1) {
            m_body = bodies.first();
        } else {
            QJsonArray array;
            for (const QJsonObject &b : bodies)
                array << b;
            m_body = array;
        }
    }

    // The same entry as a try whose answer got lost: the same key, the server replays.
    const QByteArray signature = m_path.toUtf8()
        + (m_body.isArray() ? QJsonDocument(m_body.toArray()) : QJsonDocument(m_body.toObject())).toJson(QJsonDocument::Compact);
    if (signature != m_signature) {
        m_signature = signature;
        m_key = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    }
    m_busy = true;
    m_error.clear();
    Q_EMIT changed();
    send();
}

void Entry::send()
{
    m_session->post(m_path, m_body, [this, g = m_session->generation()](const ApiReply &r) {
        if (g != m_session->generation())
            return;
        m_busy = false;
        if (!r.ok()) {
            m_error = Errors::describe(r);
            Q_EMIT changed();
            return;
        }
        m_signature.clear();
        m_key.clear();
        Q_EMIT changed();
        Q_EMIT saved();
    }, m_key);
}
