#include "EffectRegistry.h"

#include "../effects/FliesEffect.h"
#include "../effects/OozeEffect.h"
#include "../effects/TentacleEffect.h"

namespace hyperbin::effects {

const QVector<EffectInfo> &all()
{
    // Menu order. Add new effects here and nowhere else.
    static const QVector<EffectInfo> kAll = {
        { QStringLiteral("flies"), QStringLiteral("Flies"),
          [] { return std::unique_ptr<Effect>(new FliesEffect); } },
        { QStringLiteral("ooze"), QStringLiteral("Ooze"),
          [] { return std::unique_ptr<Effect>(new OozeEffect); } },
        { QStringLiteral("tentacles"), QStringLiteral("Tentacles"),
          [] { return std::unique_ptr<Effect>(new TentacleEffect); } },
    };
    return kAll;
}

QString defaultId()
{
    return all().isEmpty() ? QString() : all().first().id;
}

bool exists(const QString &id)
{
    for (const EffectInfo &e : all())
        if (e.id == id)
            return true;
    return false;
}

std::unique_ptr<Effect> create(const QString &id)
{
    for (const EffectInfo &e : all())
        if (e.id == id)
            return e.create();
    // Unknown id — a settings file from a newer build, or a typo. Degrade
    // to the first effect rather than to a blank overlay.
    return all().isEmpty() ? nullptr : all().first().create();
}

} // namespace hyperbin::effects
