#include "SparkleUpdater.h"

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

#include <QDebug>

namespace {

/// Bridges Sparkle's KVO-based `canCheckForUpdates` to a Qt signal.
///
/// The feed URL and the public key are both read from Info.plist
/// (SUFeedURL / SUPublicEDKey), so there is nothing to configure here —
/// this exists only so the menu entry can grey itself out while a check
/// is in flight.
} // namespace

@interface HBUpdaterDelegate : NSObject
@property (nonatomic, strong) SPUStandardUpdaterController *controller;
@property (nonatomic, assign) hyperbin::SparkleUpdater *owner;
@end

@implementation HBUpdaterDelegate

- (void)startObserving
{
    [self.controller.updater
        addObserver:self
         forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))
            options:(NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew)
            context:nil];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context
{
    if ([keyPath isEqualToString:NSStringFromSelector(@selector(canCheckForUpdates))]) {
        if (self.owner)
            emit self.owner->canCheckForUpdatesChanged(
                self.controller.updater.canCheckForUpdates);
        return;
    }
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}

- (void)dealloc
{
    [self.controller.updater
        removeObserver:self
            forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))];
}

@end

namespace hyperbin {
namespace {
inline HBUpdaterDelegate *del(void *p) { return (__bridge HBUpdaterDelegate *)p; }
} // namespace

SparkleUpdater::SparkleUpdater(QObject *parent)
    : AppUpdater(parent)
{
    @autoreleasepool {
        HBUpdaterDelegate *d = [[HBUpdaterDelegate alloc] init];
        d.owner = this;
        // startingUpdater:YES begins the background schedule immediately.
        // Both the feed URL and the EdDSA public key come from
        // Info.plist, which is also what Sparkle's own installer reads —
        // keeping them in one place means a feed move can't leave the
        // two disagreeing.
        d.controller = [[SPUStandardUpdaterController alloc]
            initWithStartingUpdater:YES
                    updaterDelegate:nil
                 userDriverDelegate:nil];
        [d startObserving];
        m_delegate = (__bridge_retained void *)d;
    }
}

SparkleUpdater::~SparkleUpdater()
{
    @autoreleasepool {
        del(m_delegate).owner = nullptr;
        CFRelease(m_delegate);
        m_delegate = nullptr;
    }
}

bool SparkleUpdater::canCheckForUpdates() const
{
    @autoreleasepool {
        return del(m_delegate).controller.updater.canCheckForUpdates;
    }
}

void SparkleUpdater::checkForUpdates()
{
    @autoreleasepool {
        // An accessory app is not in the activation order, so Sparkle's
        // window would open behind whatever the user was looking at —
        // and this one was asked for from a menu they just clicked.
        [NSApp activateIgnoringOtherApps:YES];
        [del(m_delegate).controller checkForUpdates:nil];
    }
}

void SparkleUpdater::setAutomaticChecksEnabled(bool enabled)
{
    @autoreleasepool {
        del(m_delegate).controller.updater.automaticallyChecksForUpdates = enabled;
    }
}

AppUpdater *AppUpdater::create(QObject *parent)
{
    return new SparkleUpdater(parent);
}

} // namespace hyperbin
