// Draws the swarm as one batched geometry node — a single draw call for
// every fly, no per-fly QQuickItem, no QML bindings in the hot path.
//
// Deliberately NOT a shader-per-fly or a Rive scene: at 128x128 the fill
// cost is irrelevant, so the only thing worth optimising is the number
// of frames and the number of draw calls. This gets both to the floor.
#pragma once

#include "../core/FlySim.h"

#include <QElapsedTimer>
#include <QImage>
#include <QQuickItem>
#include <QTimer>

class QSGTexture;

namespace hyperbin {

class FlyItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal fullness READ fullness WRITE setFullness NOTIFY fullnessChanged)
    Q_PROPERTY(int frameIntervalMs READ frameIntervalMs WRITE setFrameIntervalMs
                   NOTIFY frameIntervalMsChanged)
    Q_PROPERTY(QRectF binRect READ binRect WRITE setBinRect NOTIFY binRectChanged)

public:
    explicit FlyItem(QQuickItem *parent = nullptr);

    qreal fullness() const { return m_sim.fullness(); }
    void  setFullness(qreal f);

    /// 0 stops the clock entirely — no frames, no wakeups.
    int  frameIntervalMs() const { return m_intervalMs; }
    void setFrameIntervalMs(int ms);

    /// Rect of the bin icon in item-local coords; the swarm stays over it.
    QRectF binRect() const { return m_binRect; }
    void   setBinRect(const QRectF &r);

    /// The bin's own artwork, composited over the flies so they pass
    /// behind it. Pass a null image to draw nothing on top.
    void setBinIcon(const QImage &img);

    bool swarmIdle() const { return m_sim.isIdle(); }
    /// True once the pointer has landed on the bin AND the swarm has
    /// finished scattering. There is nothing left to animate at that
    /// point, so the caller stops rendering entirely.
    bool scattered() const { return m_scattered; }

signals:
    void fullnessChanged();
    void frameIntervalMsChanged();
    void binRectChanged();
    void swarmWentIdle();
    void scatteredChanged(bool scattered);

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;

private:
    void tick();
    /// Cursor in item-local pixels, and whether it is over the bin.
    QPointF cursorLocal() const;
    void    setScattered(bool s);
    /// Turn the bin artwork into the coarse walkable-surface grid the
    /// sim needs. See the note in the implementation — this is what keeps
    /// "landed" in the sim and "visible" on screen meaning the same thing.
    void    rebuildSurface();
    /// Cheap poll that runs ONLY while scattered, purely to notice the
    /// pointer leaving. Rendering is stopped then, so without it nothing
    /// would ever wake the swarm back up.
    void watchTick();

    /// The fly artwork, drawn procedurally once at startup rather than
    /// shipped as an asset: body, wings, and a light rim that keeps it
    /// readable against both a dark Dock and a light desktop.
    static QImage buildSprite(int px);

    FlySim        m_sim;
    QRectF        m_binRect;
    QImage        m_sprite;
    QImage        m_binIcon;
    bool          m_binIconDirty = false;
    QSGTexture   *m_texture = nullptr;     // sprite atlas
    QSGTexture   *m_maskTexture = nullptr; // bin silhouette
    QTimer        m_clock;
    QTimer        m_watch;
    QElapsedTimer m_dt;
    int           m_intervalMs = 0;
    bool          m_wasIdle    = true;
    bool          m_scattered  = false;
};

} // namespace hyperbin
