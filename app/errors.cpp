#include "errors.h"

#include <QCoreApplication>

namespace Errors {

QString describe(int status, const QString &code)
{
    auto tr = [](const char *text) { return QCoreApplication::translate("Errors", text); };
    if (code.isEmpty())
        return {};
    if (code == QLatin1String("kladde.keychain"))
        return tr("The sign-in is not kept for the next start: the system keychain did not answer.");
    if (code == QLatin1String("kladde.no_tokens"))
        return tr("The server answered without a sign-in.");
    if (status == 0)
        return tr("No connection to the server.");
    if (code.startsWith(QLatin1String("auth.mfa")))
        return tr("The code is not right or has expired.");
    if (code.startsWith(QLatin1String("auth.refresh")))
        return tr("The sign-in has ended. Please sign in again.");
    switch (status) {
    case 401:
        return tr("Not accepted: the e-mail, user name or password is not right.");
    case 403:
        return tr("This account may not do that (%1).").arg(code);
    case 404:
        return tr("Not found at this address. Is it a HealthLog?");
    case 429:
        return tr("Too many attempts. Please wait a moment.");
    default:
        break;
    }
    if (status >= 500)
        return tr("The server has a problem (HTTP %1). Please try again later.").arg(status);
    return tr("Error: %1").arg(code);
}

} // namespace Errors
