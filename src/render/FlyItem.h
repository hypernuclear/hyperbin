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

    /// Rect of the bin icon in item-local coords; the swarm orbits it.
    QRectF binRect() const { return m_binRect; }
    void   setBinRect(const QRectF &r);

    bool swarmIdle() const { return m_sim.isIdle(); }

signals:
    void fullnessChanged();
    void frameIntervalMsChanged();
    void binRectChanged();
    void swarmWentIdle();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;

private:
    void tick();

    /// The fly artwork, drawn procedurally once at startup rather than
    /// shipped as an asset: body, wings, and a light rim that keeps it
    /// readable against both a dark Dock and a light desktop.
    static QImage buildSprite(int px);

    FlySim        m_sim;
    QRectF        m_binRect;
    QImage        m_sprite;
    QSGTexture   *m_texture = nullptr; // owned by the node's material
    QTimer        m_clock;
    QElapsedTimer m_dt;
    int           m_intervalMs = 0;
    bool          m_wasIdle    = true;
};

} // namespace hyperbin
