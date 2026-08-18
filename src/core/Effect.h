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

#include "BinMouth.h"

#include <QMargins>
#include <QtGlobal>
#include <QObject>
#include <QRectF>
#include <QImage>
#include <QUrl>
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

    /// Where the bin's OPENING is — the ellipse you could reach into.
    ///
    /// Everything else here describes the bin's outline. This describes
    /// the hole in the top of it, which the outline cannot: the near lip
    /// runs THROUGH the silhouette, not around it. An effect that has
    /// something come out of the bin needs it, both to know where the way
    /// out is and to know what should pass behind the front wall on the
    /// way; an effect that only crawls over the outside can ignore it.
    ///
    /// Measured from the artwork, not assumed — see core/BinMouth. Check
    /// `measured` before leaning on it hard; it falls back to a generic
    /// bin rather than refusing to answer.
    virtual void setMouth(const BinMouth &mouth) { Q_UNUSED(mouth); }
    /// The bin's artwork itself, in colour.
    ///
    /// Node-based effects get this as a texture in updateNode() and can
    /// ignore this. A QML-hosted effect cannot — it never sees that
    /// texture — and the ooze needs the pixels in order to refract them.
    virtual void setBinImage(const QImage &img) { Q_UNUSED(img); }

    /// Pointer position in host-item coordinates. Polled, not delivered:
    /// the overlay is click-through and never receives hover events.
    virtual void setCursor(const QPointF &pos, bool present) = 0;

    // --- clock ----------------------------------------------------------

    /// Advance by `dt` seconds. Never called while isAtRest() is true.
    virtual void step(float dt) = 0;
    // There is deliberately no per-effect frame interval here.
    //
    // There was: effects could name a slowest-acceptable cadence and the
    // host took the slower of that and the policy's. It existed as a cost
    // lever for effects that never rest — but NO effect in this app rests
    // (every isAtRest() collapses to isEmpty()), so it was not
    // distinguishing anything. What it actually did was let one effect
    // quietly opt out of the policy: the gel asked for 33ms and got 30fps
    // on a 120Hz display, and the tentacles did the same until it was
    // noticed.
    //
    // Cadence now has one owner, core/PowerPolicy, which runs at the
    // display's refresh rate and halves it when conserving. An effect that
    // is too expensive at full rate is too expensive, and the fix is the
    // effect, not a private cap the policy cannot see.

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
    //
    // An effect draws one of two ways, and says which by whether it
    // returns a visual source:
    //
    //   * a scene-graph NODE, built in updateNode(). Cheapest, and right
    //     for anything 2D — the swarm is one textured quad per batch.
    //   * a QML COMPONENT, named here. Needed for Qt Quick 3D, whose
    //     View3D is an item subtree and cannot be expressed as a node.
    //     The effect object itself is handed to the component as
    //     `effect`, so the QML binds to its properties directly.
    //
    // Returning a source makes updateNode() irrelevant; the host hosts
    // the component instead and never calls it.
    /// QML component to instantiate, or empty to draw with updateNode().
    virtual QUrl visualSource() const { return {}; }

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
