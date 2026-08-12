// The list of effects, in one place.
//
// Settings store the id string, not an enum ordinal: ordinals silently
// change meaning when the list is reordered, and this list is expected to
// grow. The menu is built from this too, so adding an effect is one entry
// here and nothing else.
#pragma once

#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace hyperbin {

class Effect;

struct EffectInfo
{
    QString id;      ///< stable, lowercase, used as the settings key
    QString label;   ///< shown in the menu
    std::function<std::unique_ptr<Effect>()> create;
};

namespace effects {

/// Every registered effect, in menu order.
const QVector<EffectInfo> &all();

/// Construct by id. Falls back to the first registered effect for an
/// unknown id — a settings file naming an effect this build doesn't have
/// should degrade to something rather than to nothing.
std::unique_ptr<Effect> create(const QString &id);

/// The id used when nothing is stored.
QString defaultId();

/// Whether an id is one we can build.
bool exists(const QString &id);

} // namespace effects
} // namespace hyperbin
