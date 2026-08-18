#include "LowPower.h"

#include <QTimer>

#include <windows.h>

namespace hyperbin {
namespace {

/// Battery Saver, from the flags GetSystemPowerStatus already returns.
///
/// SYSTEM_STATUS_FLAG_POWER_SAVING_ON is bit 0 of SystemStatusFlag, added
/// in Windows 10. On anything older the field reads 0, which lands on
/// "not conserving" — the same answer a machine without the feature
/// should give.
bool querySaver()
{
    SYSTEM_POWER_STATUS s{};
    if (!GetSystemPowerStatus(&s))
        return false;
    return (s.SystemStatusFlag & 0x01) != 0;
}

} // namespace

LowPowerWatch::LowPowerWatch(QObject *parent)
    : QObject(parent)
{
    m_active = querySaver();

    // Polled rather than hooked. The event route is
    // RegisterPowerSettingNotification with GUID_POWER_SAVING_STATUS, and
    // it needs an HWND with a WndProc to deliver WM_POWERBROADCAST to —
    // this app's only window is a Qt Quick overlay that is destroyed and
    // recreated whenever the effect stops, so the hook would keep having
    // to be re-registered against a moving target. A five-second poll of
    // a call that reads a cached struct is not worth avoiding.
    auto *t = new QTimer(this);
    t->setInterval(5000);
    connect(t, &QTimer::timeout, this, [this] { set(querySaver()); });
    t->start();
    m_token = nullptr;
}

LowPowerWatch::~LowPowerWatch() = default;

void LowPowerWatch::set(bool v)
{
    if (v == m_active)
        return;
    m_active = v;
    emit activeChanged(v);
}

} // namespace hyperbin
