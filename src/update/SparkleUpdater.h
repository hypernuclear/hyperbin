// Sparkle 2, wrapped so the rest of the app never sees Objective-C.
#pragma once

#include "AppUpdater.h"

namespace hyperbin {

class SparkleUpdater : public AppUpdater
{
    Q_OBJECT
public:
    explicit SparkleUpdater(QObject *parent = nullptr);
    ~SparkleUpdater() override;

    bool canCheckForUpdates() const override;

public slots:
    void checkForUpdates() override;
    void setAutomaticChecksEnabled(bool enabled) override;

private:
    // Opaque handle to the Objective-C delegate, which owns the
    // SPUStandardUpdaterController. Kept as void* so this header stays
    // includable from plain C++.
    void *m_delegate = nullptr;
};

} // namespace hyperbin
