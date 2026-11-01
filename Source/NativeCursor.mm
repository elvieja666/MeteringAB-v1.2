#import <AppKit/AppKit.h>
#include "NativeCursor.h"

namespace NativeCursor
{
    void showClosedHand() { [[NSCursor closedHandCursor] set]; }
    void showOpenHand()   { [[NSCursor openHandCursor] set]; }
}
