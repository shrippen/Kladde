#include "connection.h"

#include <QSettings>
#include <QUrl>

Connection::Connection(QObject *parent)
    : QObject(parent)
{
    m_server = QSettings().value(QStringLiteral("server")).toString();
    connect(&m_client, &kladde::HealthLogClient::versionReceived, this,
            [this](bool ok, const QString &version, const QString &error) {
        m_state = ok ? State::Reachable : State::Unreachable;
        m_version = version;
        m_error = error;
        Q_EMIT stateChanged();
    });
}

void Connection::setServer(const QString &server)
{
    const QString trimmed = server.trimmed();
    if (trimmed == m_server)
        return;
    m_server = trimmed;
    QSettings().setValue(QStringLiteral("server"), m_server);
    m_state = State::Unknown;
    Q_EMIT serverChanged();
    Q_EMIT stateChanged();
}

void Connection::check()
{
    const QUrl url = QUrl::fromUserInput(m_server);
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        m_state = State::Unreachable;
        m_error = tr("Enter an https:// address");
        Q_EMIT stateChanged();
        return;
    }
    m_state = State::Checking;
    m_error.clear();
    Q_EMIT stateChanged();
    m_client.setServer(url);
    m_client.fetchVersion();
}
