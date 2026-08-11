// Single owner of "should we be drawing right now, and how fast."
//
// Every rule from docs/battery.md lives here rather than being smeared
// through the render path, so the power behaviour can be reasoned about
// and tested in one place.
#pragma once

#include <QObject>

namespace hyperbin {

class PowerPolicy : public QObject
{
    Q_OBJECT
public:
    explicit PowerPolicy(QObject *parent = nullptr);

    // Inputs — each setter is cheap and idempotent.
    void setBinEmpty(bool empty);
    void setSwarmIdle(bool idle);      // flies fully dispersed
    void setTargetVisible(bool vis);   // not occluded / not hidden
    void setDisplayAwake(bool awake);  // display on, session unlocked
    void setOnBattery(bool battery);
    void setLowPowerMode(bool low);

    /// 0 means "don't render at all" — the caller stops its timer and
    /// tears the overlay surface down rather than drawing empty frames.
    int  frameIntervalMs() const;
    bool shouldRender() const;

signals:
    void changed();

private:
    void bump();

    bool m_binEmpty      = true;
    bool m_swarmIdle     = true;
    bool m_targetVisible = true;
    bool m_displayAwake  = true;
    bool m_onBattery     = false;
    bool m_lowPower      = false;
    bool m_lastRender    = false;
    int  m_lastInterval  = 0;
};

} // namespace hyperbin
