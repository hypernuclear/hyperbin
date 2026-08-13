// Ooze: sludge coating the bin and dripping off it.
//
// Owns the bin's signed distance field — built from the icon's own
// silhouette — and hands it to the shader, which does the rest.
#pragma once

#include "../core/Effect.h"
#include "../core/OozeSim.h"

#include <QImage>
#include <QVector>

class QSGTexture;

namespace hyperbin {

class OozeEffect : public Effect
{
    Q_OBJECT
public:
    explicit OozeEffect(QObject *parent = nullptr);
    ~OozeEffect() override;

    void setBinRect(const QRectF &binRect) override;
    void setSurface(const QVector<quint8> &coverage, int w, int h) override;
    void setFullness(float fullness) override;
    void setContentLine(float y01) override { m_contentLine = y01; }
    void setCursor(const QPointF &pos, bool present) override;
    void step(float dt) override;

    bool isEmpty() const override { return m_sim.isEmpty(); }
    bool isAtRest() const override { return m_sim.isAtRest(); }
    int  preferredFrameIntervalMs() const override
    {
        return m_sim.preferredFrameIntervalMs();
    }

    QMargins margins(qreal iconSize) const override;

    QSGNode *updateNode(QSGNode *old, QQuickWindow *window,
                        const QRectF &binRect, QSGTexture *mask) override;
    void releaseResources() override;

    const OozeSim &sim() const { return m_sim; }

private:
    OozeSim     m_sim;
    QImage      m_field;             // signed distance field of the bin
    bool        m_fieldDirty = false;
    QSGTexture *m_fieldTex   = nullptr;
    float       m_fieldRangeCells = 0.0f;
    bool        m_wasEmpty   = true;
    float       m_contentLine = 0.22f;
};

} // namespace hyperbin
