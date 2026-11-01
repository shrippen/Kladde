#pragma once

#include "session.h"

#include <QString>

// What went wrong, in words for people and in the app's language. HealthLog sends error
// codes (meta.errorCode) meant for its own bundle; the app names the cases itself, mostly
// by HTTP status, and shows the code only where it knows no better.
namespace Errors {

// status: HTTP status, 0 for no answer, -1 for a problem of the app itself.
QString describe(int status, const QString &code);
inline QString describe(const kladde::ApiReply &r) { return describe(r.status, r.error()); }

} // namespace Errors
