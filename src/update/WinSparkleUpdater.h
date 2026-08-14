// WinSparkle, wrapped to the same shape as the macOS side.
#pragma once

#include "AppUpdater.h"

namespace hyperbin {

class WinSparkleUpdater : public AppUpdater
{
    Q_OBJECT
public:
    explicit WinSparkleUpdater(QObject *parent = nullptr);
    ~WinSparkleUpdater() override;

    bool canCheckForUpdates() const override;

public slots:
    void checkForUpdates() override;
    void setAutomaticChecksEnabled(bool enabled) override;

private:
    // WinSparkle's callbacks are plain C function pointers with no user
    // data, so the one live instance has to be reachable statically.
    // There is exactly one updater per process, created in main().
    static WinSparkleUpdater *s_instance;
    static int  onCanShutdown();
    static void onShutdownRequest();
};

} // namespace hyperbin
