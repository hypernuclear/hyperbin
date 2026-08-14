// The `create()` for a build configured with HYPERBIN_ANALYTICS=OFF.
//
// A separate file rather than an #ifdef inside CountlyAnalytics.cpp,
// because the point of the option is that the SDK is not compiled or
// linked at all — an #ifdef would still need the submodule present.
#include "Analytics.h"

namespace hyperbin {

Analytics *Analytics::create(QObject *) { return nullptr; }

} // namespace hyperbin
