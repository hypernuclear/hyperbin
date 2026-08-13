// The contract every animation implements.
//
// One effect is live at a time — they are alternatives, not layers, and
// nothing here supports compositing two of them. Adding one means
// implementing this interface and registering it; it means touching
// neither the host item, nor the menu, nor the power policy.
//
// Read docs/effects.md before writing one. The three rules that are easy
// to get wrong and expensive to get wrong are all about STOPPING:
//
//   * isEmpty()  — nothing is drawn, so the overlay can be torn down.
//   * isAtRest() — something IS drawn but it will not change, so the
//                  frame can stay on screen with the clock stopped.
//   * neither    — we are animating and paying for it.
//
// The swarm only ever needed the first. An ooze puddle that has settled
// needs the second: a static frame costs nothing to keep, but redrawing
// it sixty times a second costs exactly as much as real animation.
#pragma once

#include <QMargins>
#include <QtGlobal>
#include <QObject>
#include <QRectF>
#include <QVector>

class QQuickWindow;
class QSGNode;
class QSGTexture;

namespace hyperbin {

class Effect : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~Effect() override = default;

    // --- inputs ---------------------------------------------------------

    /// The bin's bounding rect in the host item's coordinates.
    virtual void setBinRect(const QRectF &binRect) = 0;

    /// The bin's SILHOUETTE as a coverage grid over binRect: 1 where the
    /// artwork is solid, 0 where it is not. The host builds this from the
    /// icon's own alpha, so it matches what the mask shader will clip
    /// against — an effect that uses the bounding rect instead will place
    /// things where the renderer then erases them. Empty grid means "not
    /// known yet"; fall back to the rect rather than refusing to draw.
    virtual void setSurface(const QVector<quint8> &coverage, int w, int h) = 0;

    /// 0 = empty bin, 1 = as full as the settings consider full.
    virtual void setFullness(float fullness) = 0;

    /// Where the bin's CONTENTS start, as a fraction of the icon's height
    /// from its top. 0 would be the very top of the artwork; a typical
    /// trash icon puts its rubbish somewhere around a fifth of the way
    /// down, below the rim.
    ///
    /// Derived from the artwork rather than guessed — see EffectItem for
    /// how, and why it works on both platforms. Effects that do not care
    /// where the rubbish sits can ignore it.
    virtual void setContentLine(float y01) { Q_UNUSED(y01); }

    /// Pointer position in host-item coordinates. Polled, not delivered:
    /// the overlay is click-through and never receives hover events.
    virtual void setCursor(const QPointF &pos, bool present) = 0;

    // --- clock ----------------------------------------------------------

    /// Advance by `dt` seconds. Never called while isAtRest() is true.
    virtual void step(float dt) = 0;
    /// The slowest frame interval this effect is happy with, in ms.
    /// 0 means "whatever the power policy says".
    ///
    /// For an effect that genuinely never rests — ooze bubbles for as
    /// long as there is trash — this is the only lever left on its cost.
    /// The policy still decides whether to draw AT ALL; this only asks
    /// for less often when it does.
    virtual int preferredFrameIntervalMs() const { return 0; }

    // --- power ----------------------------------------------------------

    /// Nothing is drawn. The host may hide the overlay entirely.
    virtual bool isEmpty() const = 0;

    /// The current frame is final: keep it on screen, stop the clock.
    /// Effects with nothing persistent (flies) can return isEmpty().
    virtual bool isAtRest() const = 0;

    /// The pointer is on the bin and the effect has finished reacting to
    /// it — the swarm has scattered, the ooze has recoiled. Distinct from
    /// isEmpty() because it is temporary and reverses when the pointer
    /// leaves, which the host watches for on a slow timer.
    virtual bool isDismissed() const { return false; }

    // --- geometry -------------------------------------------------------

    /// Room this effect needs AROUND the icon, at the given icon size in
    /// px. The host sizes the overlay window from this, so anything drawn
    /// outside it is clipped by the window and looks broken.
    virtual QMargins margins(qreal iconSize) const = 0;

    // --- rendering ------------------------------------------------------

    /// Build or update the scene-graph node.
    ///
    /// `old` is always either null or a node THIS effect returned
    /// earlier, so casting it back to whatever shape you built is safe.
    /// The host discards another effect's node on a switch.
    ///
    /// `mask` is the bin's own
    /// artwork as a texture, for the three occlusion modes (behind the
    /// bin / clipped to it / in front); it may be null before the icon
    /// has loaded. Ownership of the returned node passes to the caller.
    virtual QSGNode *updateNode(QSGNode *old, QQuickWindow *window,
                                const QRectF &binRect, QSGTexture *mask) = 0;

    /// Drop any GPU resources. Called on the render thread when the
    /// effect is swapped out or the window goes away.
    virtual void releaseResources() {}

signals:
    /// isEmpty() or isAtRest() changed. The host re-evaluates whether to
    /// keep rendering; emitting spuriously is harmless, not emitting is
    /// a stuck animation or a stuck timer.
    void activityChanged();
};

} // namespace hyperbin
