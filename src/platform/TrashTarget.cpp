#include "TrashTarget.h"
#include "StubTrashTarget.h"

#include <QCoreApplication>

#if defined(Q_OS_MACOS)
#include "DockTrashTarget.h"
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
    // TODO(HYP-252): RecycleBinTarget — SHQueryRecycleBin for the count,
    // desktop list view for the icon rect.
    return std::make_unique<StubTrashTarget>(parent);
#else
    return std::make_unique<StubTrashTarget>(parent);
#endif
}

} // namespace hyperbin
