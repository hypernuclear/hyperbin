// The one QQuickItem the overlay contains, and the host for whichever
// Effect is currently selected.
//
// It knows nothing about flies, ooze, or anything else. Its whole job is
// the things every effect needs and none of them should re-implement:
//
//   * the clock, and stopping it when the effect says it is at rest;
//   * the bin's rect and its silhouette, derived from the icon's alpha;
//   * the mask texture the occlusion modes draw against;
//   * polling the pointer, because the overlay is click-through and
//     never receives hover events;
//   * noticing the pointer leave while nothing is being drawn.
//
// Deliberately NOT a QQuickItem per particle and not a Rive scene: at
// these sizes fill cost is irrelevant, so the only things worth
// optimising are the number of frames and the number of draw calls.
#pragma once

#include "../core/Effect.h"

#include <QElapsedTimer>
#include <QImage>
#include <QQuickItem>
#include <QTimer>
#include <QQmlComponent>
#include <memory>

class QSGTexture;

namespace hyperbin {

class EffectItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal fullness READ fullness WRITE setFullness NOTIFY fullnessChanged)
    Q_PROPERTY(int frameIntervalMs READ frameIntervalMs WRITE setFrameIntervalMs
                   NOTIFY frameIntervalMsChanged)
    Q_PROPERTY(QRectF binRect READ binRect WRITE setBinRect NOTIFY binRectChanged)
    Q_PROPERTY(QString effectId READ effectId WRITE setEffectId NOTIFY effectIdChanged)

public:
    explicit EffectItem(QQuickItem *parent = nullptr);
    ~EffectItem() override;

    /// Which effect is running. Setting it swaps the effect out, keeping
    /// the bin state so the new one starts already knowing where it is.
    QString effectId() const { return m_effectId; }
    void    setEffectId(const QString &id);

    qreal fullness() const { return m_fullness; }
    void  setFullness(qreal f);

    /// 0 stops the clock entirely — no frames, no wakeups.
    int  frameIntervalMs() const { return m_intervalMs; }
    void setFrameIntervalMs(int ms);

    /// Rect of the bin icon in item-local coords.
    QRectF binRect() const { return m_binRect; }
    void   setBinRect(const QRectF &r);

    /// The bin's own artwork. Used two ways: as the occlusion mask, and
    /// as the source of the walkable-surface grid effects place things
    /// on. Pass a null image when it isn't known.
    void setBinIcon(const QImage &img);

    /// Room the current effect needs around the icon, at this icon size.
    /// The overlay window is sized from this.
    QMargins margins(qreal iconSize) const;
    /// Nothing is drawn — the caller may tear the overlay down.
    bool isEmpty() const { return !m_effect || m_effect->isEmpty(); }
    /// The frame is final: keep it on screen, stop the clock.
    bool isAtRest() const { return !m_effect || m_effect->isAtRest(); }
    /// The pointer is on the bin and the effect has finished reacting.
    bool isDismissed() const { return m_dismissed; }

signals:
    void fullnessChanged();
    void frameIntervalMsChanged();
    void binRectChanged();
    void effectIdChanged();
    /// Nothing left to draw.
    void becameEmpty();
    /// isAtRest() flipped — the host of THIS item decides whether to keep
    /// the clock running.
    void restStateChanged(bool atRest);
    void dismissedChanged(bool dismissed);

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void geometryChange(const QRectF &newGeom, const QRectF &oldGeom) override;

private:
    void tick();
    /// Cursor in item-local pixels.
    QPointF cursorLocal() const;
    void    setDismissed(bool d);
    /// Cheap poll that runs ONLY while dismissed, purely to notice the
    /// pointer leaving. Rendering is stopped then, so without it nothing
    /// would ever wake the effect back up.
    void watchTick();
    /// Turn the bin artwork into the coarse surface grid effects place
    /// things on. This is what keeps "on the bin" in a simulation and
    /// "visible" on screen meaning the same thing.
    void rebuildSurface();
    /// Find where the bin's contents begin, from the artwork's own alpha.
    float detectContentLine() const;
    void applyBinState();
    /// Create or tear down the QML visual when the effect changes.
    void rebuildVisual();
    void applyFrameInterval();
    /// Clear the rest state — an input changed, so there is work to do.
    void wake();

    /// The instantiated QML visual, for effects that draw that way.
    /// Owned by this item as a child; null for node-based effects.
    QQuickItem *m_visual = nullptr;
    std::unique_ptr<QQmlComponent> m_visualComponent;
    std::unique_ptr<Effect> m_effect;
    QString       m_effectId;
    QRectF        m_binRect;
    QImage        m_binIcon;
    qreal         m_fullness     = 0.0;
    bool          m_binIconDirty = false;
    QSGTexture   *m_maskTexture  = nullptr; // bin silhouette, owned here
    /// Which effect built the current scene-graph node. Effects each
    /// build their own node shape and cast `old` to it, so handing one
    /// effect's node to another is a crash — see updatePaintNode().
    const Effect *m_nodeOwner = nullptr;
    QTimer        m_clock;
    QTimer        m_watch;
    QElapsedTimer m_dt;
    /// What the power policy last asked for, and what is actually in
    /// force. Two numbers because the effect gets to slow the clock down
    /// and effects can be swapped underneath it — with only the second,
    /// a slow effect's floor outlived the effect that set it.
    int           m_requestedMs = 0;
    int           m_intervalMs = 0;
    bool          m_wasEmpty   = true;
    bool          m_wasAtRest  = true;
    bool          m_dismissed  = false;
};

} // namespace hyperbin
