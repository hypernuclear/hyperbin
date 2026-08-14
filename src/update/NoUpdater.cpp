// The `create()` that runs when there is no updater to create.
//
// Compiled on platforms Sparkle and WinSparkle don't cover, and on any
// build configured with HYPERBIN_AUTO_UPDATE=OFF. Returning null is the
// documented answer; callers hide their update UI and carry on.
#include "AppUpdater.h"

namespace hyperbin {

AppUpdater *AppUpdater::create(QObject *) { return nullptr; }

} // namespace hyperbin
