#include "FlyItem.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTexture>
#include <QSGTextureMaterial>
#include <QtMath>

namespace hyperbin {

namespace {
// One textured quad per fly, rotated to face its heading. Still a single
// draw call for the whole swarm.
constexpr int kVertsPerFly = 6;

// Sprite size in device pixels. The fly is drawn ~7px on screen, so 32
// gives headroom for retina and for the scale variation between flies.
constexpr int kSpritePx = 32;
} // namespace

QImage FlyItem::buildSprite(int px)
{
    QImage img(px, px, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal c  = px * 0.5;
    const qreal bw = px * 0.30; // body half-length (points along +x)
    const qreal bh = px * 0.19; // body half-width

    // Wings first, swept back from the shoulders, so the body overlaps
    // their roots. Translucent and cool-toned — a fly's wings are the
    // only bright thing on it.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(240, 244, 250, 150));
    for (int s : {-1, 1}) {
        QPainterPath wing;
        wing.moveTo(c - bw * 0.1, c + s * bh * 0.4);
        wing.cubicTo(c - bw * 1.5, c + s * bh * 2.6,
                     c - bw * 2.2, c + s * bh * 1.4,
                     c - bw * 0.9, c + s * bh * 0.5);
        p.drawPath(wing);
    }

    // The body must be a MID tone, not black. Two earlier attempts failed
    // for the same reason: a dark body contributes nothing against a dark
    // wallpaper, so whatever outline you put around it is all that's left
    // and the fly reads as a hollow ring. A mid grey is darker than a
    // light desktop and lighter than a dark one, so the filled shape
    // itself carries on both; the thin dark rim then defines the edge
    // against light backgrounds.
    p.setPen(QPen(QColor(16, 16, 20, 210), px * 0.055));
    p.setBrush(QColor(126, 128, 136));
    p.drawEllipse(QPointF(c, c), bw, bh);

    // Head, slightly proud of the body at the front.
    p.setBrush(QColor(150, 152, 160));
    p.drawEllipse(QPointF(c + bw * 0.78, c), bh * 0.72, bh * 0.72);

    p.end();
    return img;
}

FlyItem::FlyItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    m_sprite = buildSprite(kSpritePx);
    setFlag(ItemHasContents, true);
    m_clock.setTimerType(Qt::PreciseTimer);
    connect(&m_clock, &QTimer::timeout, this, &FlyItem::tick);
}

void FlyItem::setFullness(qreal f)
{
    if (qFuzzyCompare(m_sim.fullness(), float(f)))
        return;
    m_sim.setFullness(float(f));
    emit fullnessChanged();
}

void FlyItem::setBinRect(const QRectF &r)
{
    if (r == m_binRect)
        return;
    m_binRect = r;
    m_sim.setBinRect(r);
    emit binRectChanged();
}

void FlyItem::setFrameIntervalMs(int ms)
{
    if (ms == m_intervalMs)
        return;
    m_intervalMs = ms;

    if (ms <= 0) {
        m_clock.stop();          // no timer, no wakeups, no frames
    } else {
        m_clock.setInterval(ms);
        if (!m_clock.isActive()) {
            m_dt.restart();
            m_clock.start();
        }
    }
    emit frameIntervalMsChanged();
}

void FlyItem::tick()
{
    const float dt = m_dt.isValid() ? float(m_dt.restart()) / 1000.0f : 0.05f;
    m_sim.step(dt);

    // Tell the owner the moment the last fly leaves, so it can drop the
    // frame rate to zero and tear down the overlay surface.
    const bool idle = m_sim.isIdle();
    if (idle && !m_wasIdle)
        emit swarmWentIdle();
    m_wasIdle = idle;

    update(); // the ONLY place a repaint is requested
}

QSGNode *FlyItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    const auto &flies = m_sim.flies();
    if (flies.isEmpty()) {
        delete old;
        return nullptr;
    }

    auto *node = static_cast<QSGGeometryNode *>(old);
    if (!node) {
        node = new QSGGeometryNode;
        auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0);
        g->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(g);
        node->setFlag(QSGNode::OwnsGeometry);

        m_texture = window()->createTextureFromImage(
            m_sprite, QQuickWindow::TextureHasAlphaChannel);
        auto *m = new QSGTextureMaterial;
        m->setTexture(m_texture);
        m->setFiltering(QSGTexture::Linear);
        node->setMaterial(m);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    QSGGeometry *g = node->geometry();
    g->allocate(flies.size() * kVertsPerFly);
    auto *v = g->vertexDataAsTexturedPoint2D();

    int i = 0;
    for (const Fly &f : flies) {
        // Wingbeat squashes the sprite across its heading — at any frame
        // rate it's the difference between "a dot" and "a fly".
        const float beat = 0.82f + 0.18f * std::sin(f.phase);
        // Size off the icon, not a constant: Dock magnification and the
        // user's Dock-size setting both change the tile, and fixed-size
        // flies next to a magnified bin look wrong.
        const float k    = m_sim.sizeScale();
        const float hw   = 5.4f * f.scale * k;
        const float hh   = 5.4f * f.scale * beat * k;
        const float x    = float(f.pos.x());
        const float y    = float(f.pos.y());

        // Face the direction of travel; fall back to the wander heading
        // when nearly stationary so the sprite never snaps to 0.
        const float sp = float(std::hypot(f.vel.x(), f.vel.y()));
        const float ang = sp > 1.0f ? std::atan2(float(f.vel.y()), float(f.vel.x()))
                                    : f.wanderAngle;
        const float ca = std::cos(ang), sa = std::sin(ang);

        auto put = [&](float lx, float ly, float u, float w) {
            v[i].set(x + lx * ca - ly * sa, y + lx * sa + ly * ca, u, w);
            ++i;
        };
        put(-hw, -hh, 0, 0); put(hw, -hh, 1, 0); put(hw, hh, 1, 1);
        put(-hw, -hh, 0, 0); put(hw, hh, 1, 1); put(-hw, hh, 0, 1);
    }

    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

} // namespace hyperbin
