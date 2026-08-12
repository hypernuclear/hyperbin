#include "TrashTarget.h"
#include "StubTrashTarget.h"

#include <QCoreApplication>

#if defined(Q_OS_MACOS)
#include "DockTrashTarget.h"
#elif defined(Q_OS_WIN)
#include "RecycleBinTarget.h"
#endif

namespace hyperbin {

std::unique_ptr<TrashTarget> TrashTarget::create(QObject *parent)
{
    // --stub forces the fake target on any platform, for development.
    if (QCoreApplication::arguments().contains(QStringLiteral("--stub")))
        return std::make_unique<StubTrashTarget>(parent);

#if defined(Q_OS_MACOS)
    return std::make_unique<DockTrashTarget>(parent);
#elif defined(Q_OS_WIN)
    return std::make_unique<RecycleBinTarget>(parent);
#else
    return std::make_unique<StubTrashTarget>(parent);
#endif
}

} // namespace hyperbin
