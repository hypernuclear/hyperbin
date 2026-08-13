#include "FliesEffect.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTexture>
#include <QtMath>
#include "../render/FlyMaskMaterial.h"

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
} // namespace

FliesEffect::FliesEffect(QObject *parent)
    : Effect(parent)
    , m_sprite(buildSprite(kSpritePx))
{
}

void FliesEffect::setBinRect(const QRectF &binRect)
{
    m_sim.setBinRect(binRect);
}

void FliesEffect::setSurface(const QVector<quint8> &coverage, int w, int h)
{
    m_sim.setSurface(coverage, w, h);
}

void FliesEffect::setFullness(float fullness)
{
    m_sim.setFullness(fullness);
}

void FliesEffect::setCursor(const QPointF &pos, bool present)
{
    m_sim.setCursor(pos, present);
}

void FliesEffect::step(float dt)
{
    m_sim.step(dt);

    // Flies leave rather than settling, so "nothing to draw" is the only
    // activity change this effect has. An effect that leaves something
    // behind reports isAtRest() separately — see docs/effects.md.
    const bool empty = isEmpty();
    if (empty != m_wasEmpty) {
        m_wasEmpty = empty;
        emit activityChanged();
    }
}

QMargins FliesEffect::margins(qreal iconSize) const
{
    // Asymmetric: flies rise well above the bin but barely stray to
    // either side, and nothing occluded may pass below it.
    const int x = FlySim::marginX(iconSize);
    return QMargins(x, FlySim::marginTop(iconSize),
                    x, FlySim::marginBottom(iconSize));
}

void FliesEffect::releaseResources()
{
    delete m_texture;
    m_texture = nullptr;
}

QImage FliesEffect::buildSprite(int px)
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
        //
        // Plus a dark rim, because that stopped being true on Windows: the
        // Recycle Bin is pale, and white-on-pale wings vanished completely,
        // leaving a black dot with no insect around it. The rim is carried
        // by the wings alone — the body is already near-black and needs no
        // help — and it is a rim, not an outline around the whole fly,
        // which would read as a sticker. Real wings have dark veins and a
        // darker leading edge, so this adds detail rather than cartooning
        // it. It also survives the downscale: the sprite is drawn at 32px
        // and shown at roughly a third of that, so a 1.6px stroke lands as
        // a soft edge rather than a hard line.
        // SHORT AND BROAD, not long and thin. The wing used to be a
        // tapering cubic sliver sweeping most of a body-length back —
        // which is the right shape for a fly you can see, and the wrong
        // one for a fly drawn at 32px and shown at 8. A sliver one texture
        // pixel wide at its ends averages away to nothing in the
        // downscale, and what survived was a black dot with no insect
        // around it. A paddle roughly as long as the body and a third as
        // wide keeps its area when it shrinks, and at full size still
        // reads as a wing because the sweep angle — not the outline — is
        // what says "wing".
        //
        // Ellipses rather than paths for the same reason: at this size
        // the silhouette is all that survives, and an ellipse's is
        // predictable at any scale.
        // Stretch a little as it swings out, as well as rotating. Rotation
        // alone is a few pixels of arc at the size this is actually drawn,
        // which is not enough to read as a beat — the old sliver got its
        // flap from changing length, and losing that entirely would have
        // traded a visible fly for a static one.
        const qreal wingLen  = bw * 0.78 * (0.88 + 0.18 * spread); // half-length
        const qreal wingWid  = bh * 0.70;                          // half-width
        // Folded back at rest, swung out at the top of the beat. Angle is
        // measured off the body's axis, so the flap reads as rotation
        // rather than as the wing changing shape.
        const qreal sweepDeg = 18.0 + 42.0 * spread;
        p.setPen(QPen(QColor(20, 20, 26, 105), px * 0.05));
        p.setBrush(QColor(255, 255, 255, int(105 + 60 * spread)));
        for (int s : {-1, 1}) {
            p.save();
            p.translate(c - bw * 0.15, cy + s * bh * 0.45);   // shoulder
            p.rotate(s * (180.0 - sweepDeg));
            // Hinged at the shoulder: the ellipse is centred a full
            // half-length out, so the wing root stays at the body.
            p.drawEllipse(QPointF(wingLen, 0.0), wingLen, wingWid);
            p.restore();
        }
        p.setPen(Qt::NoPen);   // the body and head are fills, not strokes

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

    // HYPERBIN_DUMP_SPRITE=<path.png> writes the atlas out. The fly is a
    // dozen pixels across in situ, so "is the wing rim actually there or
    // did the pen get swallowed" is not a question the screen can answer.
    if (qEnvironmentVariableIsSet("HYPERBIN_DUMP_SPRITE"))
        img.save(qEnvironmentVariable("HYPERBIN_DUMP_SPRITE"));
    return img;
}

QSGNode *FliesEffect::updateNode(QSGNode *old, QQuickWindow *window,
                                 const QRectF &binRect, QSGTexture *mask)
{
    const auto &flies = m_sim.flies();
    if (flies.isEmpty()) {
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
        m_texture = window->createTextureFromImage(
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
    // The mask texture belongs to the host — it is built from the bin
    // icon, which every effect needs and none of them owns. Assigning it
    // every frame is free; the material's compare() decides whether
    // anything actually has to be re-uploaded.
    matBehind->mask  = mask;
    matSurface->mask = mask;
    const bool dbg = qEnvironmentVariableIsSet("HYPERBIN_SHOW_BINRECT");
    matBehind->binRect = binRect;
    matBehind->debugRect = dbg;
    matBehind->maskMode = 1;           // hidden where the bin is
    matSurface->binRect = binRect;
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
    // pointer to mask that the mask update then deleted, and
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
    if (!calAxis.isEmpty() && !binRect.isEmpty()) {
        // Range comes from HYPERBIN_CALIBRATE=x8 / y6 etc; the first
        // sweep sat entirely off the bin, so a fixed +/-4 was useless.
        // Sweep scales with the icon by default: a fixed span in points
        // is a wide spread over a 22pt bin and a barely-visible nudge
        // over an 87pt one. An explicit number still overrides it.
        // Scale sweep is in percent; position sweeps are in points and
        // scale with the icon.
        float span = calAxis.startsWith('s')
            ? 8.0f
            : 0.11f * float(qMin(binRect.width(), binRect.height()));
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
                  binRect.width(), binRect.height(),
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
                const QPointF ctr = binRect.center();
                r = QRectF(0, 0, binRect.width() * f2, binRect.height() * f2);
                r.moveCenter(ctr);
            } else {
                r = binRect.translated(xAxis ? kCand[ci] : 0.0,
                                         xAxis ? 0.0 : kCand[ci]);
            }
            m2->mask = mask;
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
    const bool showRect = dbg && !binRect.isEmpty();

    int nBehind = showRect ? 1 : 0, nSurface = 0, nFront = 0;
    for (const Fly &f : flies) {
        if (f.clipToBin)    ++nSurface;
        else if (f.inFront || f.onSurface) ++nFront;
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
        const float l = float(binRect.left()),  t = float(binRect.top());
        const float r = float(binRect.right()), b = float(binRect.bottom());
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
        // A crawler draws in the FRONT batch — unoccluded, uncut. It is
        // standing on the near face of the bin; nothing is in front of
        // it. Only one that has walked its centre off the silhouette goes
        // in the clipped batch.
        auto *dst = f.clipToBin ? vs : ((f.inFront || f.onSurface) ? vf : vb);
        int  &idx = f.clipToBin ? js : ((f.inFront || f.onSurface) ? jf : i);
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
