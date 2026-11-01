#pragma once

#include "secretstore.h"

// The refresh token in the desktop keychain (KWallet or Secret Service via QtKeychain).
class KeychainStore : public kladde::SecretStore
{
public:
    void read(const QString &key, ReadDone done) override;
    void write(const QString &key, const QString &value, Done done) override;
    void remove(const QString &key, Done done) override;
};
