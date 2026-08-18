#include "LowPower.h"

#import <Foundation/Foundation.h>

namespace hyperbin {

LowPowerWatch::LowPowerWatch(QObject *parent)
    : QObject(parent)
{
    m_active = [[NSProcessInfo processInfo] isLowPowerModeEnabled];

    // The notification is the whole point: Low Power Mode turns itself on
    // when the battery reaches 20%, so a value read once at launch is
    // wrong for exactly the users who care most.
    id token = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSProcessInfoPowerStateDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *) {
                    set([[NSProcessInfo processInfo] isLowPowerModeEnabled]);
                }];
    m_token = (__bridge_retained void *)token;
}

LowPowerWatch::~LowPowerWatch()
{
    if (m_token) {
        id token = (__bridge_transfer id)m_token;
        [[NSNotificationCenter defaultCenter] removeObserver:token];
        m_token = nullptr;
    }
}

void LowPowerWatch::set(bool v)
{
    if (v == m_active)
        return;
    m_active = v;
    emit activeChanged(v);
}

} // namespace hyperbin
