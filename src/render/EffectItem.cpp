#include "EffectItem.h"

#include "../core/EffectRegistry.h"

#include <QCursor>
#include <QQuickWindow>
#include <QSGTexture>

namespace hyperbin {

namespace {
// How often to check whether the pointer has left the bin, while the
// effect is dismissed and nothing is being drawn. 200ms is imperceptible
// for this and is the only wakeup the app has in that state.
constexpr int kWatchMs = 200;

// Resolution of the walkable-surface grid handed to effects. Coarse on
// purpose: it answers "is the bin here", not "what does the bin look
// like", and anything an effect places is bigger than one cell.
constexpr int kCoverage = 24;

// Alpha above which a pixel counts as solid bin. Matches the shader's own
// cutoff for "the bin is here" (smoothstep 0.25..0.65 in flymask.frag)
// closely enough that nothing is ever placed where it gets clipped.
constexpr int kSolidAlpha = 115;
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
void EffectItem::applyBinState()
{
    if (!m_effect)
        return;
    m_effect->setBinRect(m_binRect);
    m_effect->setFullness(float(m_fullness));
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
    if (!m_effect) {
        delete old;
        return nullptr;
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
