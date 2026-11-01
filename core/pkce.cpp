#include "pkce.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace kladde {

namespace {

constexpr int VerifierLength = 64; // RFC 7636: 43 … 128
constexpr char Unreserved[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

} // namespace

QByteArray pkceVerifier()
{
    QByteArray v(VerifierLength, Qt::Uninitialized);
    auto *random = QRandomGenerator::system();
    for (char &c : v) {
        c = Unreserved[random->bounded(int(sizeof(Unreserved) - 1))];
    }
    return v;
}

QByteArray pkceChallenge(const QByteArray &verifier)
{
    return QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

} // namespace kladde
