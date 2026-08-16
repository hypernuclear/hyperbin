#include "EffectItem.h"

#include "BinMouth.h"

#include "../core/EffectRegistry.h"

#include <QCursor>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGTexture>

namespace hyperbin {

namespace {
// How often to check whether the pointer has left the bin, while the
// effect is dismissed and nothing is being drawn. 200ms is imperceptible
// for this and is the only wakeup the app has in that state.
constexpr int kWatchMs = 200;

// Resolution of the surface grid handed to effects.
//
// Was 24, which is ample for "is the bin here" — the question the swarm
// asks — but the ooze builds a distance field out of it, and a field
// measured from a 24-cell staircase renders the bin as a staircase. At 96
// the taper of the bin is a curve. Costs one 9KB pass per icon change.
constexpr int kCoverage = 96;

// Alpha above which a pixel counts as solid bin. Matches the shader's own
// cutoff for "the bin is here" (smoothstep 0.25..0.65 in flymask.frag)
// closely enough that nothing is ever placed where it gets clipped.
constexpr int kSolidAlpha = 115;

// Alpha above which a pixel is the bin's CONTENTS rather than the bin.
//
// Both shells draw a translucent container with opaque rubbish inside it:
// the macOS Dock trash is a grey mesh cylinder you can see through, and
// the Windows Recycle Bin's paper sticks up out of a similar shell. So
// "nearly opaque, inside the silhouette" is the rubbish, on both, at any
// size and in either theme — no per-platform constant, and it tracks the
// full/empty artwork automatically.
constexpr int kContentAlpha = 232;
} // namespace

EffectItem::EffectItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_clock.setTimerType(Qt::PreciseTimer);
    connect(&m_clock, &QTimer::timeout, this, &EffectItem::tick);
    // Coarse on purpose: it exists to notice a pointer leaving, and a
    // coarse timer can be coalesced with other wakeups by the OS.
    m_watch.setTimerType(Qt::CoarseTimer);
    m_watch.setInterval(kWatchMs);
    connect(&m_watch, &QTimer::timeout, this, &EffectItem::watchTick);

    setEffectId(effects::defaultId());
}

EffectItem::~EffectItem() = default;

void EffectItem::setEffectId(const QString &id)
{
    if (id == m_effectId && m_effect)
        return;
    m_effectId = id;
    m_effect = effects::create(id);
    // The new effect starts knowing where the bin is and how full it is,
    // so switching does not produce a frame of it guessing.
    applyBinState();
    m_wasEmpty  = true;
    m_wasAtRest = true;
    setDismissed(false);
    wake();   // a freshly-swapped effect has work to do
    if (m_effect) {
        connect(m_effect.get(), &Effect::activityChanged, this, [this] {
            const bool empty  = m_effect->isEmpty();
            const bool atRest = m_effect->isAtRest();
            if (empty && !m_wasEmpty)
                emit becameEmpty();
            if (atRest != m_wasAtRest)
                emit restStateChanged(atRest);
            m_wasEmpty  = empty;
            m_wasAtRest = atRest;
        });
    }
    rebuildVisual();
    // The new effect may want a different clock from the old one.
    applyFrameInterval();
    emit effectIdChanged();
    update();
}

void EffectItem::wake()
{
    // Any change to the effect's inputs invalidates rest: whatever it had
    // settled into was settled for the OLD bin. Without this an effect
    // that has come to rest never notices the trash changing, because the
    // clock that would tell it is exactly what rest switched off.
    if (!m_wasAtRest)
        return;
    m_wasAtRest = false;
    emit restStateChanged(false);
}
void EffectItem::rebuildVisual()
{
    delete m_visual;
    m_visual = nullptr;
    m_visualComponent.reset();
    const QUrl src = m_effect ? m_effect->visualSource() : QUrl();
    if (src.isEmpty())
        return;   // node-based effect; updatePaintNode does the work
    QQmlEngine *engine = qmlEngine(this);
    if (!engine) {
        qWarning("hyperbin: no QML engine; cannot host %s's visual",
                 qPrintable(m_effectId));
        return;
    }
    m_visualComponent = std::make_unique<QQmlComponent>(engine, src, this);
    if (m_visualComponent->isError()) {
        // Loudly: a silent failure here is an effect that renders nothing
        // and looks exactly like one that is switched off.
        qWarning("hyperbin: %s visual failed to load: %s", qPrintable(m_effectId),
                 qPrintable(m_visualComponent->errorString()));
        return;
    }
    // The effect object goes in as `effect`, so the component binds to
    // its properties rather than the host having to shuttle values.
    QVariantMap props;
    props.insert(QStringLiteral("effect"),
                 QVariant::fromValue(static_cast<QObject *>(m_effect.get())));
    QObject *obj = m_visualComponent->createWithInitialProperties(
        props, qmlContext(this));
    m_visual = qobject_cast<QQuickItem *>(obj);
    if (!m_visual) {
        qWarning("hyperbin: %s visual is not an Item", qPrintable(m_effectId));
        delete obj;
        return;
    }
    m_visual->setParentItem(this);
    m_visual->setParent(this);
    m_visual->setX(0);
    m_visual->setY(0);
    m_visual->setWidth(width());
    m_visual->setHeight(height());
}
void EffectItem::applyBinState()
{
    if (!m_effect)
        return;
    m_effect->setBinRect(m_binRect);
    m_effect->setFullness(float(m_fullness));
    m_effect->setBinImage(m_binIcon);
    rebuildSurface();
}

void EffectItem::setFullness(qreal f)
{
    if (qFuzzyCompare(m_fullness, f))
        return;
    m_fullness = f;
    if (m_effect)
        m_effect->setFullness(float(f));
    wake();
    emit fullnessChanged();
}

void EffectItem::setBinRect(const QRectF &r)
{
    if (r == m_binRect)
        return;
    m_binRect = r;
    if (m_effect)
        m_effect->setBinRect(r);
    wake();
    emit binRectChanged();
}

void EffectItem::setBinIcon(const QImage &img)
{
    m_binIcon = img;
    m_binIconDirty = true;
    if (m_effect)
        m_effect->setBinImage(img);
    rebuildSurface();
    wake();
    update();
}

void EffectItem::rebuildSurface()
{
    if (!m_effect)
        return;
    // Hand the effect the same silhouette the shader clips against.
    //
    // These two used to disagree: the fly simulation landed flies
    // anywhere inside the bin's bounding rect, while the clip mask keeps
    // only what covers the artwork's alpha. The trash artwork covers well
    // under half of its own tile, so most landed flies stood on empty
    // tile and were erased — a crowded simulation and an empty screen.
    // Every effect gets this for free now, and none of them should be
    // reaching for the bounding rect instead.
    if (m_binIcon.isNull()) {
        m_effect->setSurface({}, 0, 0);
        return;
    }
    const QImage src = m_binIcon.scaled(kCoverage, kCoverage, Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_ARGB32);
    QVector<quint8> cov(kCoverage * kCoverage, 0);
    for (int y = 0; y < kCoverage; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < kCoverage; ++x)
            cov[y * kCoverage + x] = qAlpha(row[x]) > kSolidAlpha ? 1 : 0;
    }
    m_effect->setSurface(cov, kCoverage, kCoverage);
    m_effect->setContentLine(detectContentLine());
    // The opening, measured off the FULL-resolution artwork rather than
    // the coverage grid: the lip is a shading edge a few pixels thick,
    // and the grid is 1-bit alpha at a fraction of the size. It would not
    // survive either conversion.
    const BinMouth mouth = measureBinMouth(m_binIcon);
    m_effect->setMouth(mouth);
    m_mouth = mouth;
    emit mouthChanged();
}

float EffectItem::detectContentLine() const
{
    // Fallback for an unknown icon: a fifth of the way down is about
    // where the rubbish sits in both shells' artwork.
    constexpr float kFallback = 0.22f;
    if (m_binIcon.isNull())
        return kFallback;

    const QImage src = m_binIcon.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    if (w < 4 || h < 4)
        return kFallback;

    // The topmost row with a meaningful RUN of near-opaque pixels. A run,
    // not a single pixel, because the rim's antialiasing and the lid's
    // own highlight produce isolated opaque specks well above the
    // rubbish — one stray pixel would put the fill line at the very top,
    // which is the thing being fixed.
    const int need = qMax(3, w / 12);
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        int run = 0;
        for (int x = 0; x < w; ++x) {
            run = qAlpha(row[x]) >= kContentAlpha ? run + 1 : 0;
            if (run >= need)
                return float(y) / float(h);
        }
    }
    return kFallback;
}

QMargins EffectItem::margins(qreal iconSize) const
{
    // A sane fallback rather than zero: an overlay with no margin clips
    // the effect against the icon's own edge, which reads as broken.
    if (!m_effect)
        return QMargins(int(iconSize), int(iconSize), int(iconSize), int(iconSize));
    return m_effect->margins(iconSize);
}
void EffectItem::setFrameIntervalMs(int ms)
{
    m_requestedMs = ms;
    applyFrameInterval();
}
void EffectItem::applyFrameInterval()
{
    // The policy decides whether to draw at all; the effect may ask to be
    // drawn less often when we do. Only ever slower, never faster — 0
    // still means stop.
    //
    // Recomputed from the REQUEST every time, and re-run whenever the
    // effect changes. Folding the effect's floor straight into the stored
    // interval meant it outlived the effect that asked for it: ooze wants
    // 33ms, the policy wants 16, and the policy only signals when its own
    // answer moves — so switching from ooze to flies left the flies
    // running at half rate until something unrelated happened to change
    // the power state.
    int ms = m_requestedMs;
    if (ms > 0 && m_effect)
        ms = qMax(ms, m_effect->preferredFrameIntervalMs());
    if (ms == m_intervalMs)
        return;
    m_intervalMs = ms;
    if (qEnvironmentVariableIsSet("HYPERBIN_DEBUG"))
        qInfo("hyperbin: clock %dms (policy asked %d, '%s' floor %d)", ms,
              m_requestedMs, qPrintable(m_effectId),
              m_effect ? m_effect->preferredFrameIntervalMs() : 0);

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

void EffectItem::geometryChange(const QRectF &newGeom, const QRectF &oldGeom)
{
    QQuickItem::geometryChange(newGeom, oldGeom);
    if (m_visual) {
        m_visual->setWidth(newGeom.width());
        m_visual->setHeight(newGeom.height());
    }
}
void EffectItem::tick()
{
    if (!m_effect)
        return;
    const float dt = m_dt.isValid() ? float(m_dt.restart()) / 1000.0f : 0.05f;
    // Cursor position is POLLED, not received as an event: the overlay is
    // click-through by design, so it never gets hover or move events at
    // all — whatever is beneath it does. One QCursor::pos() per frame is
    // cheap next to the frame itself.
    m_effect->setCursor(cursorLocal(), true);
    m_effect->step(dt);

    setDismissed(m_effect->isDismissed());

    const bool empty  = m_effect->isEmpty();
    const bool atRest = m_effect->isAtRest();
    if (empty && !m_wasEmpty)
        emit becameEmpty();
    if (atRest != m_wasAtRest)
        emit restStateChanged(atRest);
    m_wasEmpty  = empty;
    m_wasAtRest = atRest;

    update(); // the ONLY place a repaint is requested
}

QPointF EffectItem::cursorLocal() const
{
    return mapFromGlobal(QPointF(QCursor::pos()));
}

void EffectItem::setDismissed(bool d)
{
    if (d == m_dismissed)
        return;
    m_dismissed = d;
    // The watch runs only while dismissed. Rendering is stopped in that
    // state, so this is the only thing left awake — and the moment the
    // pointer leaves it stops again.
    if (d)
        m_watch.start();
    else
        m_watch.stop();
    emit dismissedChanged(d);
}

void EffectItem::watchTick()
{
    if (!m_binRect.contains(cursorLocal())) {
        // Tell the effect first: it decides what "no longer dismissed"
        // means, and only then do we report the state back out.
        if (m_effect)
            m_effect->setCursor(cursorLocal(), true);
        setDismissed(false);
    }
}

QSGNode *EffectItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    // A QML-hosted effect draws through its own item subtree, so this
    // item contributes no geometry of its own.
    if (m_effect && !m_effect->visualSource().isEmpty()) {
        delete old;
        return nullptr;
    }
    if (!m_effect) {
        delete old;
        return nullptr;
    }

    // Never hand one effect the node another one built.
    //
    // Each effect creates its own node shape and casts `old` back to it —
    // flies use a root with three geometry children, ooze a single quad —
    // so passing a foreign node across an effect switch reads a material
    // that was never there. That is an immediate bad access, and it is
    // the host's job to prevent because the host is what swapped them.
    if (m_nodeOwner != m_effect.get()) {
        delete old;
        old = nullptr;
        m_nodeOwner = m_effect.get();
    }
    // The mask texture is built here, not in the effect: it comes from
    // the bin icon, which every effect needs and none of them owns.
    if (m_binIconDirty) {
        m_binIconDirty = false;
        delete m_maskTexture;
        m_maskTexture = m_binIcon.isNull()
            ? nullptr
            : window()->createTextureFromImage(m_binIcon,
                                               QQuickWindow::TextureHasAlphaChannel);
    }

    return m_effect->updateNode(old, window(), m_binRect, m_maskTexture);
}

void EffectItem::releaseResources()
{
    if (m_effect)
        m_effect->releaseResources();
    delete m_maskTexture;
    m_maskTexture = nullptr;
    QQuickItem::releaseResources();
}

} // namespace hyperbin
