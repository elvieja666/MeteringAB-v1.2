#import <AppKit/AppKit.h>
#include "NativeCursor.h"

namespace NativeCursor
{
    juce::MouseCursor closedHand()
    {
        NSCursor* cursor = [NSCursor closedHandCursor];
        NSImage* image = [cursor image];
        const NSSize size = [image size];
        const NSPoint hot = [cursor hotSpot];
        if (image == nil || size.width < 1.0 || size.height < 1.0)
            return juce::MouseCursor::DraggingHandCursor;

        // Al doble de resolución, para que se vea nítida en pantallas Retina.
        constexpr int k = 2;
        const int w = (int) std::ceil (size.width * k), h = (int) std::ceil (size.height * k);
        juce::Image pixels (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        {
            juce::Image::BitmapData data (pixels, juce::Image::BitmapData::writeOnly);
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate (data.data, (size_t) w, (size_t) h, 8, (size_t) data.lineStride, space,
                                                      (uint32_t) kCGImageAlphaPremultipliedFirst | (uint32_t) kCGBitmapByteOrder32Little);
            CGColorSpaceRelease (space);
            if (ctx == nullptr)
                return juce::MouseCursor::DraggingHandCursor;

            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext: [NSGraphicsContext graphicsContextWithCGContext: ctx flipped: NO]];
            [image drawInRect: NSMakeRect (0, 0, w, h) fromRect: NSZeroRect operation: NSCompositingOperationSourceOver fraction: 1.0];
            [NSGraphicsContext restoreGraphicsState];
            CGContextRelease (ctx);
        }
        return juce::MouseCursor (juce::ScaledImage (pixels, (double) k),
                                  { juce::roundToInt (hot.x), juce::roundToInt (hot.y) });
    }
}
