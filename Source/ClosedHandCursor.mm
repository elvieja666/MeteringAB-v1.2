// Mano cerrada de macOS como cursor de JUCE: se dibuja la imagen del sistema
// (a 2x, para pantallas Retina) y se usa con el mismo punto de agarre.
#include "ClosedHandCursor.h"
#import <AppKit/AppKit.h>

juce::MouseCursor makeClosedHandCursor()
{
    @autoreleasepool
    {
        NSCursor* nsCursor = [NSCursor closedHandCursor];
        NSImage* image = nsCursor != nil ? [nsCursor image] : nil;
        if (image == nil)
            return juce::MouseCursor::DraggingHandCursor;

        const NSSize size = [image size];
        const NSPoint hot = [nsCursor hotSpot];
        constexpr int scale = 2;
        const int w = (int) std::ceil (size.width * scale), h = (int) std::ceil (size.height * scale);
        if (w <= 0 || h <= 0)
            return juce::MouseCursor::DraggingHandCursor;

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nullptr
                                                                         pixelsWide: w
                                                                         pixelsHigh: h
                                                                      bitsPerSample: 8
                                                                    samplesPerPixel: 4
                                                                           hasAlpha: YES
                                                                           isPlanar: NO
                                                                     colorSpaceName: NSDeviceRGBColorSpace
                                                                        bytesPerRow: 0
                                                                       bitsPerPixel: 0];
        if (rep == nil)
            return juce::MouseCursor::DraggingHandCursor;

        [rep setSize: size];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext: [NSGraphicsContext graphicsContextWithBitmapImageRep: rep]];
        [image drawInRect: NSMakeRect (0, 0, size.width, size.height)
                 fromRect: NSZeroRect
                operation: NSCompositingOperationSourceOver
                 fraction: 1.0];
        [NSGraphicsContext restoreGraphicsState];

        NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
        [rep release];
        if (png == nil)
            return juce::MouseCursor::DraggingHandCursor;

        const auto picture = juce::PNGImageFormat::loadFrom ([png bytes], (size_t) [png length]);
        if (! picture.isValid())
            return juce::MouseCursor::DraggingHandCursor;

        const int hx = juce::jlimit (0, juce::jmax (0, (int) size.width - 1), (int) hot.x);
        const int hy = juce::jlimit (0, juce::jmax (0, (int) size.height - 1), (int) hot.y);
        return juce::MouseCursor (juce::ScaledImage (picture, (double) scale), { hx, hy });
    }
}
