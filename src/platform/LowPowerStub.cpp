#include "LowPower.h"

namespace hyperbin {

// No such notion here, so the OS is never asking us to conserve. The menu's
// Auto lands on "off", and On/Off still work — the user's own choice does
// not depend on the platform having an opinion.
LowPowerWatch::LowPowerWatch(QObject *parent) : QObject(parent) {}
LowPowerWatch::~LowPowerWatch() = default;

void LowPowerWatch::set(bool v)
{
    if (v == m_active)
        return;
    m_active = v;
    emit activeChanged(v);
}

} // namespace hyperbin
