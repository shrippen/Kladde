#include "keychainstore.h"

#include <qt6keychain/keychain.h>

namespace {

const auto Service = QStringLiteral("Kladde");

} // namespace

void KeychainStore::read(const QString &key, ReadDone done)
{
    auto *job = new QKeychain::ReadPasswordJob(Service);
    job->setKey(key);
    QObject::connect(job, &QKeychain::Job::finished, job, [done](QKeychain::Job *j) {
        done(j->error() == QKeychain::NoError, static_cast<QKeychain::ReadPasswordJob *>(j)->textData());
    });
    job->start();
}

void KeychainStore::write(const QString &key, const QString &value, Done done)
{
    auto *job = new QKeychain::WritePasswordJob(Service);
    job->setKey(key);
    job->setTextData(value);
    QObject::connect(job, &QKeychain::Job::finished, job,
                     [done](QKeychain::Job *j) { done(j->error() == QKeychain::NoError); });
    job->start();
}

void KeychainStore::remove(const QString &key, Done done)
{
    auto *job = new QKeychain::DeletePasswordJob(Service);
    job->setKey(key);
    QObject::connect(job, &QKeychain::Job::finished, job, [done](QKeychain::Job *j) {
        done(j->error() == QKeychain::NoError || j->error() == QKeychain::EntryNotFound);
    });
    job->start();
}
