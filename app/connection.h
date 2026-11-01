#pragma once

#include "healthlogclient.h"

#include <QObject>
#include <QString>

// The HealthLog connection as QML sees it. Phase 0: server address and a reachability
// check (GET /api/version). The token and the sync status follow in phase 1, with the
// token in the platform keychain (Lastenheft LF-02), never in a settings file.
class Connection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
public:
    enum class State { Unknown, Checking, Reachable, Unreachable };
    Q_ENUM(State)

    explicit Connection(QObject *parent = nullptr);

    QString server() const { return m_server; }
    void setServer(const QString &server);
    State state() const { return m_state; }
    QString serverVersion() const { return m_version; }
    QString error() const { return m_error; }

    Q_INVOKABLE void check();

Q_SIGNALS:
    void serverChanged();
    void stateChanged();

private:
    kladde::HealthLogClient m_client;
    QString m_server;
    State m_state = State::Unknown;
    QString m_version;
    QString m_error;
};
