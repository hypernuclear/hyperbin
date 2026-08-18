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
    /// Nothing is drawn — the overlay surface can go away.
    void setEffectIdle(bool idle);
    /// Something IS drawn but will not change: keep the frame on screen,
    /// stop the clock. An ooze puddle that has settled costs nothing to
    /// leave there and a full frame rate to redraw.
    void setEffectAtRest(bool atRest);
    void setTargetVisible(bool vis);   // not occluded / not hidden
    void setDisplayAwake(bool awake);  // display on, session unlocked
    /// Conserve: draw, but at a reduced rate. One input, not two.
    ///
    /// This replaced a pair — "on battery" and "low power mode" — that
    /// asked different questions and were both permanently false because
    /// nothing ever called them. Being on battery is not the same as
    /// wanting to conserve, so the decision belongs to the user (with an
    /// Auto that follows the OS) and arrives here already made.
    void setLowPower(bool low);
    /// The display's refresh rate. Frames are produced at this when not
    /// conserving — the app should look as smooth as the screen allows,
    /// not as smooth as some number written down in 2026.
    void setRefreshHz(qreal hz);
    /// Master switch from the menu. Off means no timer, no frames, no
    /// overlay surface and no trash polling — the app is resident but
    /// does nothing at all until it's switched back on.
    void setEnabled(bool on);
    /// The pointer is on the bin and the effect has finished reacting to
    /// it — nothing left to animate until it moves away.
    void setDismissed(bool dismissed);
    bool enabled() const { return m_enabled; }

    /// 0 means "don't render at all" — the caller stops its timer and
    /// tears the overlay surface down rather than drawing empty frames.
    int  frameIntervalMs() const;
    bool shouldRender() const;

signals:
    void changed();

private:
    void bump();

    bool m_binEmpty      = true;
    bool m_effectIdle    = true;
    // Starts FALSE, unlike the other flags. Rest is a state an effect
    // has to run in order to reach; defaulting to true deadlocks — the
    // clock is stopped because we think it is at rest, and it can never
    // report otherwise because it never gets a step.
    bool m_effectAtRest  = false;
    bool m_targetVisible = true;
    bool m_displayAwake  = true;
    bool  m_lowPower     = false;
    qreal m_refreshHz    = 60.0;
    bool m_enabled       = true;
    bool m_dismissed     = false;
    bool m_lastRender    = false;
    int  m_lastInterval  = 0;
};

} // namespace hyperbin
