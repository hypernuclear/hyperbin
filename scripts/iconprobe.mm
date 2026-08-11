// Does the NSImage -> QImage conversion actually produce the trash
// artwork? Dumps it to PNG so it can be looked at directly.
#import <AppKit/AppKit.h>
#include <cstdio>

int main()
{
    @autoreleasepool {
        for (NSString *name in @[ NSImageNameTrashFull, NSImageNameTrashEmpty ]) {
            NSImage *icon = [NSImage imageNamed:name];
            if (!icon) { std::printf("%s: NOT FOUND\n", name.UTF8String); continue; }
            std::printf("%s: size %.0fx%.0f, %lu reps\n", name.UTF8String,
                        icon.size.width, icon.size.height,
                        (unsigned long)icon.representations.count);

            const int px = 128;
            NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
                initWithBitmapDataPlanes:nullptr
                              pixelsWide:px pixelsHigh:px
                           bitsPerSample:8 samplesPerPixel:4
                                hasAlpha:YES isPlanar:NO
                          colorSpaceName:NSCalibratedRGBColorSpace
                             bytesPerRow:px * 4 bitsPerPixel:32];
            NSGraphicsContext *ctx =
                [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:ctx];
            [icon drawInRect:NSMakeRect(0, 0, px, px)
                    fromRect:NSZeroRect
                   operation:NSCompositingOperationSourceOver
                    fraction:1.0];
            [NSGraphicsContext restoreGraphicsState];

            NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                            properties:@{}];
            NSString *out = [NSString stringWithFormat:@"/tmp/icon_%@.png",
                             [name stringByReplacingOccurrencesOfString:@" "
                                                             withString:@"_"]];
            [png writeToFile:out atomically:YES];
            std::printf("  wrote %s\n", out.UTF8String);
        }
    }
    return 0;
}
