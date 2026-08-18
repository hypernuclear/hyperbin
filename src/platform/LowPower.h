// Does the OS want us to conserve right now?
//
// Both platforms have this and call it different things — macOS Low Power
// Mode, Windows Battery Saver — and both can change it while we run, so a
// one-off read at startup is not enough. This is the "Auto" behind the
// menu's Low Power Mode setting.
//
// Deliberately NOT "are we on battery". Being unplugged is not the same as
// wanting to conserve, and the old policy conflated them; a laptop on
// mains with Low Power Mode on should still be calm, and one on battery
// with it off should not be throttled behind the user's back.
#pragma once

#include <QObject>

namespace hyperbin {

class LowPowerWatch : public QObject
{
    Q_OBJECT
public:
    explicit LowPowerWatch(QObject *parent = nullptr);
    ~LowPowerWatch() override;

    /// True when the OS is in Low Power Mode / Battery Saver. Always false
    /// on platforms with no such notion, which is the right answer: "the
    /// OS is not asking us to conserve".
    bool active() const { return m_active; }

signals:
    void activeChanged(bool active);

private:
    void set(bool v);

    bool m_active = false;
    void *m_token = nullptr;   ///< platform observer, freed in the dtor
};

} // namespace hyperbin
