#include "entry.h"

#include "api.h"

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
    m_requests.clear();
    m_keys.clear();
    m_next = 0;
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

    QList<QPair<QString, QJsonObject>> requests;
    if (kind == Kind::Mood) {
        requests << qMakePair(QStringLiteral("/api/mood-entries"),
                              kladde::moodBody(qRound(v.value(QStringLiteral("value"))), at, note.trimmed()));
    } else {
        for (const QJsonObject &body : kladde::measurementBodies(core(kind), v, at, note.trimmed()))
            requests << qMakePair(QStringLiteral("/api/measurements"), body);
    }

    // Same entry as the failed try: keep its keys and go on where it stopped.
    QByteArray signature;
    for (const auto &r : requests)
        signature += r.first.toUtf8() + QJsonDocument(r.second).toJson(QJsonDocument::Compact);
    if (signature != m_signature) {
        m_signature = signature;
        m_requests = requests;
        m_keys.clear();
        for (qsizetype i = 0; i < requests.size(); ++i)
            m_keys << QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
        m_next = 0;
    }
    m_busy = true;
    m_error.clear();
    Q_EMIT changed();
    sendNext();
}

void Entry::sendNext()
{
    if (m_next >= m_requests.size()) {
        m_busy = false;
        m_signature.clear();
        m_requests.clear();
        m_keys.clear();
        m_next = 0;
        Q_EMIT changed();
        Q_EMIT saved();
        return;
    }
    const auto &request = m_requests.at(m_next);
    m_session->post(request.first, request.second, [this](const ApiReply &r) {
        if (!r.ok()) {
            m_busy = false;
            m_error = m_next > 0 ? tr("Partly saved (%1 of %2): %3").arg(m_next).arg(m_requests.size()).arg(r.error())
                                 : r.error();
            Q_EMIT changed();
            return;
        }
        ++m_next;
        sendNext();
    }, m_keys.at(m_next));
}
