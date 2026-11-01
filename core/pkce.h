#pragma once

#include <QByteArray>

namespace kladde {

// PKCE (RFC 7636, S256) for the sign-in in the browser: the app keeps the verifier, the
// browser carries only the challenge, and the code that comes back is worth nothing without
// the verifier.

// 64 random characters from the unreserved set [A-Za-z0-9-._~].
QByteArray pkceVerifier();
// base64url(SHA-256(verifier)) without padding.
QByteArray pkceChallenge(const QByteArray &verifier);

} // namespace kladde
