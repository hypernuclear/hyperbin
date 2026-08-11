#include "StubTrashTarget.h"

#include <QGuiApplication>
#include <QScreen>

namespace hyperbin {

StubTrashTarget::StubTrashTarget(QObject *parent)
    : TrashTarget(parent)
{
    // Park it where the Dock trash roughly lives, so overlay placement
    // and clipping get exercised realistically.
    if (auto *s = QGuiApplication::primaryScreen()) {
        const QRect g = s->availableGeometry();
        m_rect = QRect(g.right() - 90, g.bottom() - 70, 64, 64);
    }

    // Cycle fullness so the spawn/disperse paths are always under test.
    m_tick.setInterval(3000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        setItemCount(m_count >= 40 ? 0 : m_count + 8);
    });
}

void StubTrashTarget::start()
{
    m_tick.start();
    emit statusChanged(Status::Ok);
    emit iconRectChanged(m_rect);
    emit itemCountChanged(m_count);
}

void StubTrashTarget::stop()
{
    m_tick.stop();
}

void StubTrashTarget::setItemCount(int n)
{
    if (n == m_count)
        return;
    m_count = n;
    emit itemCountChanged(m_count);
}

} // namespace hyperbin
