#pragma once

#include "secretstore.h"

#include <QObject>
#include <QTimer>

// A secret store that answers later, in the order of the calls: like SessionVault on Android
// (its own thread) or QtKeychain on Linux.
class LaterSecretStore : public kladde::SecretStore
{
public:
    void read(const QString &key, ReadDone done) override
    {
        later([this, key, done] { m_store.read(key, done); });
    }
    void write(const QString &key, const QString &value, Done done) override
    {
        later([this, key, value, done] { m_store.write(key, value, done); });
    }
    void remove(const QString &key, Done done) override
    {
        later([this, key, done] { m_store.remove(key, done); });
    }

private:
    void later(std::function<void()> call)
    {
        QTimer::singleShot(0, &m_context, std::move(call));
    }

    kladde::MemorySecretStore m_store;
    QObject m_context;
};

