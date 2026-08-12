#include "FlyItem.h"

#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTexture>
#include "FlyMaskMaterial.h"
#include <QtMath>

namespace hyperbin {

namespace {
// One textured quad per fly, rotated to face its heading. Still a single
// draw call for the whole swarm.
constexpr int kVertsPerFly = 6;

// Sprite size in device pixels. The fly is drawn ~7px on screen, so 32
// gives headroom for retina and for the scale variation between flies.
constexpr int kSpritePx = 32;
// Wing positions in the atlas. Four is enough at this size: a flap is
// perceived as motion, not as distinct poses.
constexpr int kFlyFrames = 4;
// How often to check whether the pointer has left the bin, while the
// swarm is scattered and nothing is being drawn. 200ms is imperceptible
// for this and is the only wakeup the app has in that state.
constexpr int kWatchMs = 200;
} // namespace

QImage FlyItem::buildSprite(int px)
{
    // A horizontal atlas of kFlyFrames wing positions. The body is
    // identical in every frame; only the wings move.
    //
    // Previously a single frame was squashed vertically to fake a
    // wingbeat, which pulsed the whole insect — body, head and all —
    // rather than beating its wings. Separate frames cost one texture and
    // a UV offset, and are the difference between a throb and a flap.
    QImage img(px * kFlyFrames, px, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal bw = px * 0.26;  // body half-length (points along +x)
    const qreal bh = px * 0.165; // body half-width

    for (int frame = 0; frame < kFlyFrames; ++frame) {
        const qreal c = frame * px + px * 0.5;
        const qreal cy = px * 0.5;

        // Wing spread: 0 = folded along the body, 1 = fully out. A short
        // dwell at the extremes reads better than a pure sine, because a
        // real wingbeat spends longer at the turnaround.
        const qreal t = qreal(frame) / kFlyFrames;
        const qreal spread = 0.35 + 0.65 * (0.5 - 0.5 * std::cos(t * 2 * M_PI));

        p.setPen(Qt::NoPen);

        // Cheap drop shadow: offset ellipses at low alpha, no real blur.
        const qreal sx = px * 0.055, sy = px * 0.075;
        for (int i = 3; i >= 1; --i) {
            const qreal grow = px * 0.012 * i;
            p.setBrush(QColor(0, 0, 0, 26));
            p.drawEllipse(QPointF(c + sx, cy + sy), bw + grow, bh + grow);
        }

        // Wings, swept back from the shoulders. White and translucent —
        // on a dark backdrop they're the only part that reads at all.
        p.setBrush(QColor(255, 255, 255, int(90 + 55 * spread)));
        for (int s : {-1, 1}) {
            QPainterPath wing;
            const qreal out = 0.6 + 2.2 * spread; // how far it swings out
            wing.moveTo(c - bw * 0.1, cy + s * bh * 0.4);
            wing.cubicTo(c - bw * 1.5, cy + s * bh * out,
                         c - bw * 2.3, cy + s * bh * (out * 0.55),
                         c - bw * 0.9, cy + s * bh * 0.5);
            p.drawPath(wing);
        }

        // Black body. A deliberate reversal: a mid grey was used before
        // because black vanishes on a dark wallpaper. The wings and the
        // drop shadow carry the silhouette in that case — but if flies
        // ever disappear on a dark desktop again, this is the line.
        p.setBrush(QColor(10, 10, 12));
        p.drawEllipse(QPointF(c, cy), bw, bh);

        // Head, slightly proud of the body at the front.
        p.setBrush(QColor(24, 24, 28));
        p.drawEllipse(QPointF(c + bw * 0.78, cy), bh * 0.7, bh * 0.7);
    }

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
    // Coarse on purpose: it exists to notice a pointer leaving, and a
    // coarse timer can be coalesced with other wakeups by the OS.
    m_watch.setTimerType(Qt::CoarseTimer);
    m_watch.setInterval(kWatchMs);
    connect(&m_watch, &QTimer::timeout, this, &FlyItem::watchTick);
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

void FlyItem::setBinIcon(const QImage &img)
{
    m_binIcon = img;
    m_binIconDirty = true;
    rebuildSurface();
    update();
}

void FlyItem::rebuildSurface()
{
    // Hand the sim the same silhouette the shader clips against.
    //
    // These two used to disagree: the sim landed flies anywhere inside
    // the bin's bounding rect, while maskMode 2 clips a crawling fly to
    // the artwork's alpha. The trash artwork covers well under half of
    // its own Dock tile, so most landed flies stood on empty tile and
    // were erased — the sim reported a crowded bin and the screen showed
    // an empty one. Coarse on purpose: it is a walkable-surface test, not
    // a rendering mask, and a fly is bigger than one cell anyway.
    if (m_binIcon.isNull()) {
        m_sim.setSurface({}, 0, 0);
        return;
    }
    constexpr int kCov = 24;
    const QImage src = m_binIcon.scaled(kCov, kCov, Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_ARGB32);
    QVector<quint8> cov(kCov * kCov, 0);
    for (int y = 0; y < kCov; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < kCov; ++x)
            // Matches the shader's own cutoff for "the bin is here"
            // (smoothstep 0.25..0.65 in flymask.frag) closely enough that
            // a fly is never standing somewhere it gets clipped.
            cov[y * kCov + x] = qAlpha(row[x]) > 115 ? 1 : 0;
    }
    m_sim.setSurface(cov, kCov, kCov);
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
    // Cursor position is POLLED, not received as an event: the overlay is
    // click-through by design, so it never gets hover or move events at
    // all — the Dock beneath it does. One QCursor::pos() per frame is
    // cheap next to the frame itself.
    m_sim.setCursor(cursorLocal(), true);
    m_sim.step(dt);
    // Scattered: the pointer is on the bin and the last fly has gone.
    // Rendering can stop completely until it moves away.
    setScattered(m_sim.cursorOnBin() && m_sim.flies().isEmpty());

    // Tell the owner the moment the last fly leaves, so it can drop the
    // frame rate to zero and tear down the overlay surface.
    const bool idle = m_sim.isIdle();
    if (idle && !m_wasIdle)
        emit swarmWentIdle();
    m_wasIdle = idle;

    update(); // the ONLY place a repaint is requested
}

QPointF FlyItem::cursorLocal() const
{
    return mapFromGlobal(QPointF(QCursor::pos()));
}
void FlyItem::setScattered(bool s)
{
    if (s == m_scattered)
        return;
    m_scattered = s;
    // The watch runs only while scattered. Rendering is stopped in that
    // state, so this is the only thing left awake — and the moment the
    // pointer leaves it stops again.
    if (s)
        m_watch.start();
    else
        m_watch.stop();
    emit scatteredChanged(s);
}
void FlyItem::watchTick()
{
    if (!m_binRect.contains(cursorLocal()))
        setScattered(false);
}
QSGNode *FlyItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    const auto &flies = m_sim.flies();
    if (flies.isEmpty() && m_binIcon.isNull()) {
        delete old;
        return nullptr;
    }

    // Two batches: flies behind the bin (masked) and flies in front of it
    // (not masked). Depth is per-fly and fixed for its lifetime, so the
    // swarm reads as occupying space around the bin rather than sitting
    // flat behind it.
    //
    // Compositing a copy of the icon over everything was the earlier
    // approach and is gone: it had to track the Dock exactly, and the
    // Accessibility poll it depends on lags under magnification, so the
    // copy visibly desynced from the real icon.
    auto *root = old;
    if (!root) {
        root = new QSGNode;
        m_texture = window()->createTextureFromImage(
            m_sprite, QQuickWindow::TextureHasAlphaChannel);
        for (int pass = 0; pass < 3; ++pass) {
            auto *n = new QSGGeometryNode;
            auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0);
            g->setDrawingMode(QSGGeometry::DrawTriangles);
            n->setGeometry(g);
            n->setFlag(QSGNode::OwnsGeometry);
            auto *m = new FlyMaskMaterial;
            m->sprite = m_texture;
            n->setMaterial(m);
            n->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(n);
        }
    }
    // Draw order: behind the bin, then clipped-to-it (crawlers), then in
    // front. Three batches, three draw calls, one texture.
    auto *behind  = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
    auto *surface = static_cast<QSGGeometryNode *>(root->childAtIndex(1));
    auto *front   = static_cast<QSGGeometryNode *>(root->childAtIndex(2));
    // --- the bin mask ---------------------------------------------------
    auto *matBehind  = static_cast<FlyMaskMaterial *>(behind->material());
    auto *matSurface = static_cast<FlyMaskMaterial *>(surface->material());
    auto *matFront   = static_cast<FlyMaskMaterial *>(front->material());
    if (m_binIconDirty) {
        m_binIconDirty = false;
        delete m_maskTexture;
        m_maskTexture = m_binIcon.isNull()
            ? nullptr
            : window()->createTextureFromImage(m_binIcon,
                                               QQuickWindow::TextureHasAlphaChannel);
        matBehind->mask = m_maskTexture;
        matSurface->mask = m_maskTexture;
    }
    const bool dbg = qEnvironmentVariableIsSet("HYPERBIN_SHOW_BINRECT");
    matBehind->binRect = m_binRect;
    matBehind->debugRect = dbg;
    matBehind->maskMode = 1;           // hidden where the bin is
    matSurface->binRect = m_binRect;
    matSurface->debugRect = false;
    matSurface->maskMode = 2;          // clipped TO the bin
    matFront->mask = nullptr;          // front flies are never occluded
    matFront->binRect = QRectF();
    matFront->debugRect = false;
    matFront->maskMode = 0;
    behind->markDirty(QSGNode::DirtyMaterial);
    surface->markDirty(QSGNode::DirtyMaterial);
    front->markDirty(QSGNode::DirtyMaterial);

    // --- calibration ------------------------------------------------------
    // MUST come after the mask texture is (re)created below-of-here in
    // program order: running it first meant these materials captured a
    // pointer to m_maskTexture that the mask update then deleted, and
    // the next frame drew from freed memory (SIGSEGV).
    // HYPERBIN_CALIBRATE=x|y draws the mask silhouette at five candidate
    // offsets at once, each a different colour, so which one fits is a
    // single human judgement instead of an automated measurement.
    //
    // That indirection exists because measuring this from screenshots has
    // proved unreliable over and over: screencapture clips at display
    // edges, the Dock shifts between probe and capture, the app's own
    // flies sit on the icon, and the shape fitter locks onto the opaque
    // folder above because the trash itself is semi-transparent. An eye
    // has none of those failure modes.
    const QByteArray calAxis = qgetenv("HYPERBIN_CALIBRATE");
    if (!calAxis.isEmpty() && !m_binRect.isEmpty()) {
        // Range comes from HYPERBIN_CALIBRATE=x8 / y6 etc; the first
        // sweep sat entirely off the bin, so a fixed +/-4 was useless.
        // Sweep scales with the icon by default: a fixed span in points
        // is a wide spread over a 22pt bin and a barely-visible nudge
        // over an 87pt one. An explicit number still overrides it.
        // Scale sweep is in percent; position sweeps are in points and
        // scale with the icon.
        float span = calAxis.startsWith('s')
            ? 8.0f
            : 0.11f * float(qMin(m_binRect.width(), m_binRect.height()));
        {
            const QByteArray n = calAxis.mid(1);
            if (!n.isEmpty() && n.toFloat() > 0) span = n.toFloat();
        }
        span = qMax(span, 1.0f);
        const float kCand[5] = {-span, -span * 0.5f, 0.0f, span * 0.5f, span};
        static const float kCols[5][4] = {
            {1.0f, 0.15f, 0.15f, 1.0f},  // red    -4
            {1.0f, 0.65f, 0.0f,  1.0f},  // orange -2
            {0.1f, 0.9f,  0.2f,  1.0f},  // green   0
            {0.2f, 0.6f,  1.0f,  1.0f},  // blue   +2
            {0.85f, 0.3f, 1.0f,  1.0f},  // violet +4
        };
        // Three sweep modes. 's' scales the mask about its centre — if a
        // position sweep never lines up at either extreme, the error is
        // size, and no amount of translating will fix it.
        const bool xAxis  = calAxis.startsWith('x');
        const bool sAxis  = calAxis.startsWith('s');
        static bool legend = false;
        if (!legend) { legend = true;
            const char *unit = sAxis ? "%% size" : "pt";
            qInfo("calibrate %s over icon %.0fx%.0f: red=%+.1f%s orange=%+.1f%s "
                  "green=0 blue=%+.1f%s violet=%+.1f%s",
                  sAxis ? "scale" : (xAxis ? "dx" : "dy"),
                  m_binRect.width(), m_binRect.height(),
                  -span, unit, -span*0.5f, unit, span*0.5f, unit, span, unit); }
        while (root->childCount() < 3 + 5) {
            auto *n = new QSGGeometryNode;
            auto *g2 = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0);
            g2->setDrawingMode(QSGGeometry::DrawTriangles);
            n->setGeometry(g2);
            n->setFlag(QSGNode::OwnsGeometry);
            auto *m2 = new FlyMaskMaterial;
            m2->sprite = m_texture;
            n->setMaterial(m2);
            n->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(n);
        }
        for (int ci = 0; ci < 5; ++ci) {
            auto *n = static_cast<QSGGeometryNode *>(root->childAtIndex(3 + ci));
            auto *m2 = static_cast<FlyMaskMaterial *>(n->material());
            QRectF r;
            if (sAxis) {
                // kCand is a percentage here, not points.
                const qreal f2 = 1.0 + kCand[ci] / 100.0;
                const QPointF ctr = m_binRect.center();
                r = QRectF(0, 0, m_binRect.width() * f2, m_binRect.height() * f2);
                r.moveCenter(ctr);
            } else {
                r = m_binRect.translated(xAxis ? kCand[ci] : 0.0,
                                         xAxis ? 0.0 : kCand[ci]);
            }
            m2->mask = m_maskTexture;
            m2->binRect = r;
            m2->debugRect = true;
            memcpy(m2->debugRGBA, kCols[ci], sizeof(kCols[ci]));
            auto *g2 = n->geometry();
            g2->allocate(6);
            auto *v2 = g2->vertexDataAsTexturedPoint2D();
            const float l = float(r.left()), t = float(r.top());
            const float rr = float(r.right()), b = float(r.bottom());
            v2[0].set(l, t, 0, 0); v2[1].set(rr, t, 1, 0); v2[2].set(rr, b, 1, 1);
            v2[3].set(l, t, 0, 0); v2[4].set(rr, b, 1, 1); v2[5].set(l, b, 0, 1);
            n->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        }
    }


    // --- the swarm ------------------------------------------------------
    // HYPERBIN_SHOW_BINRECT draws one extra quad over binRect so the mask
    // region is directly visible on screen. Inferring its position from
    // screenshots proved unreliable; this shows it.
    const bool showRect = dbg && !m_binRect.isEmpty();

    int nBehind = showRect ? 1 : 0, nSurface = 0, nFront = 0;
    for (const Fly &f : flies) {
        if (f.onSurface)    ++nSurface;
        else if (f.inFront) ++nFront;
        else                ++nBehind;
    }

    QSGGeometry *g  = behind->geometry();
    QSGGeometry *gs = surface->geometry();
    QSGGeometry *gf = front->geometry();
    g->allocate(nBehind * kVertsPerFly);
    gs->allocate(nSurface * kVertsPerFly);
    gf->allocate(nFront * kVertsPerFly);
    auto *vb = g->vertexDataAsTexturedPoint2D();
    auto *vs = nSurface ? gs->vertexDataAsTexturedPoint2D() : nullptr;
    auto *vf = nFront ? gf->vertexDataAsTexturedPoint2D() : nullptr;

    int i = 0, js = 0, jf = 0;
    if (showRect) {
        const float l = float(m_binRect.left()),  t = float(m_binRect.top());
        const float r = float(m_binRect.right()), b = float(m_binRect.bottom());
        vb[i++].set(l, t, 0, 0); vb[i++].set(r, t, 1, 0); vb[i++].set(r, b, 1, 1);
        vb[i++].set(l, t, 0, 0); vb[i++].set(r, b, 1, 1); vb[i++].set(l, b, 0, 1);
    }
    for (const Fly &f : flies) {
        // Wingbeat squashes the sprite across its heading — at any frame
        // rate it's the difference between "a dot" and "a fly".
        // No whole-sprite squash any more — the wings do the work.
        const float beat = 1.0f;
        // Size off the icon, not a constant: Dock magnification and the
        // user's Dock-size setting both change the tile, and fixed-size
        // flies next to a magnified bin look wrong.
        // No size ramp on arrival/departure. Scaling a fly in and out
        // reads as it changing size for no reason — there's nothing in
        // the scene to interpret it as distance — and at ~2pt the pop it
        // was hiding isn't visible anyway.
        // Render scale is NOT the sim's scale: motion stays proportional
        // to the icon, the drawn fly does not. See FlySim::renderScale()
        // for why it is floored at the bottom and compressed at the top —
        // and note it lives there, next to the margins that must leave
        // room for it, rather than as a constant duplicated here.
        // To make flies bigger, edit FlySim::kFlyLengthAt40 — it is stated
        // in visible points, which this quad is not.
        const float rk   = float(FlySim::renderScale(
            qMax(m_sim.binRect().width(), m_sim.binRect().height())));
        const float base = float(FlySim::spriteBase());
        const float fade = 1.0f;
        const float hw   = base * f.scale * rk * fade;
        const float hh   = base * f.scale * beat * rk * fade;
        const float x    = float(f.pos.x());
        const float y    = float(f.pos.y());

        // Face the direction of travel; fall back to the wander heading
        // when nearly stationary so the sprite never snaps to 0.
        const float sp = float(std::hypot(f.vel.x(), f.vel.y()));
        const float ang = sp > 1.0f ? std::atan2(float(f.vel.y()), float(f.vel.x()))
                                    : f.wanderAngle;
        const float ca = std::cos(ang), sa = std::sin(ang);

        // Pick the wing frame. phase advances slowly while crawling and
        // fast in flight (FlySim), so the same expression gives a gentle
        // bob on the bin and a blur of wings in the air.
        const int frame = int(f.phase) % kFlyFrames;
        const float u0 = float(frame) / kFlyFrames;
        const float u1 = float(frame + 1) / kFlyFrames;
        auto *dst = f.onSurface ? vs : (f.inFront ? vf : vb);
        int  &idx = f.onSurface ? js : (f.inFront ? jf : i);
        auto put = [&](float lx, float ly, float u, float w) {
            dst[idx].set(x + lx * ca - ly * sa, y + lx * sa + ly * ca, u, w);
            ++idx;
        };
        put(-hw, -hh, u0, 0); put(hw, -hh, u1, 0); put(hw, hh, u1, 1);
        put(-hw, -hh, u0, 0); put(hw, hh, u1, 1); put(-hw, hh, u0, 1);
    }

    behind->markDirty(QSGNode::DirtyGeometry);
    surface->markDirty(QSGNode::DirtyGeometry);
    front->markDirty(QSGNode::DirtyGeometry);
    return root;
}

} // namespace hyperbin
