// Countly-backed implementation of Analytics.
#pragma once

#include "Analytics.h"

#include <QMutex>
#include <QSettings>
#include <atomic>

namespace hyperbin {

class CountlyAnalytics : public Analytics
{
    Q_OBJECT
public:
    CountlyAnalytics(const QString &appKey, const QString &serverUrl,
                     QObject *parent = nullptr);
    ~CountlyAnalytics() override;

    bool enabled() const override;
    void setEnabled(bool on) override;
    void event(const QString &name, const QVariantMap &segmentation = {}) override;
    void property(const QString &key, const QString &value) override;

private:
    /// Brings the SDK up. Called once, on the first opt-in — never
    /// before, so a user who never opts in never has a device ID
    /// computed for them, let alone sent.
    void initialise();

    QString m_appKey;
    QString m_serverUrl;
    // Read without the mutex on every event, so it has to be atomic.
    std::atomic<bool> m_enabled{false};
    bool m_initialised = false;
    mutable QMutex m_mutex;
    QSettings m_store;
};

} // namespace hyperbin
