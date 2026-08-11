// Fake target so the simulation and renderer are developable on day one,
// before either native backend exists, and so CI can run headless.
// Selected with --stub, and used as the fallback when a native backend
// can't be constructed.
#pragma once

#include "TrashTarget.h"

#include <QTimer>

namespace hyperbin {

class StubTrashTarget : public TrashTarget
{
    Q_OBJECT
public:
    explicit StubTrashTarget(QObject *parent = nullptr);

    Status status() const override { return Status::Ok; }
    QRect iconRect() const override { return m_rect; }
    int itemCount() const override { return m_count; }
    void start() override;
    void stop() override;

    /// Dev affordance: drive fullness by hand from the tray menu.
    void setItemCount(int n);

private:
    QRect  m_rect{0, 0, 64, 64};
    int    m_count = 0;
    QTimer m_tick;
};

} // namespace hyperbin
