#pragma once

#include <QHash>
#include <QString>

#include <functional>

namespace kladde {

// Where the refresh token lives (Lastenheft LF-02): the platform keychain, never a settings
// file. QtKeychain (KWallet / Secret Service) on Linux, the Android Keystore through JNI on
// Android, memory in the tests. Asynchronous because the keychains are.
class SecretStore
{
public:
    using ReadDone = std::function<void(bool ok, const QString &value)>;
    using Done = std::function<void(bool ok)>;

    virtual ~SecretStore() = default;
    // ok is false for a missing entry as well as for a keychain that does not answer.
    virtual void read(const QString &key, ReadDone done) = 0;
    virtual void write(const QString &key, const QString &value, Done done) = 0;
    virtual void remove(const QString &key, Done done) = 0;
};

class MemorySecretStore : public SecretStore
{
public:
    void read(const QString &key, ReadDone done) override
    {
        done(m_values.contains(key), m_values.value(key));
    }
    void write(const QString &key, const QString &value, Done done) override
    {
        m_values.insert(key, value);
        done(true);
    }
    void remove(const QString &key, Done done) override
    {
        m_values.remove(key);
        done(true);
    }
    QHash<QString, QString> values() const { return m_values; }

private:
    QHash<QString, QString> m_values;
};

} // namespace kladde
