// Flies: a swarm that lands on the bin, walks about, and takes off.
//
// Owns FlySim (the behaviour) and the drawing of it. Everything specific
// to flies lives here or in FlySim — the host item knows only Effect.
#pragma once

#include "../core/Effect.h"
#include "../core/FlySim.h"

#include <QImage>

class QSGTexture;

namespace hyperbin {

class FliesEffect : public Effect
{
    Q_OBJECT
public:
    explicit FliesEffect(QObject *parent = nullptr);

    void setBinRect(const QRectF &binRect) override;
    void setSurface(const QVector<quint8> &coverage, int w, int h) override;
    void setFullness(float fullness) override;
    void setCursor(const QPointF &pos, bool present) override;
    void step(float dt) override;

    /// Flies leave. Nothing persists, so "nothing to draw" and "the frame
    /// is final" are the same condition — unlike an effect that leaves a
    /// puddle behind.
    bool isEmpty() const override { return m_sim.isIdle(); }
    bool isAtRest() const override { return isEmpty(); }
    bool isDismissed() const override
    {
        return m_sim.cursorOnBin() && m_sim.flies().isEmpty();
    }

    QMargins margins(qreal iconSize) const override;

    QSGNode *updateNode(QSGNode *old, QQuickWindow *window,
                        const QRectF &binRect, QSGTexture *mask) override;
    void releaseResources() override;

    /// Test/debug access to the simulation.
    const FlySim &sim() const { return m_sim; }

private:
    /// The fly artwork, drawn procedurally once rather than shipped as an
    /// asset: body, wings, and a light rim that keeps it readable against
    /// both a dark Dock and a light desktop.
    static QImage buildSprite(int px);

    FlySim      m_sim;
    QImage      m_sprite;
    QSGTexture *m_texture = nullptr;   // sprite atlas
    bool        m_wasEmpty = true;
};

} // namespace hyperbin
