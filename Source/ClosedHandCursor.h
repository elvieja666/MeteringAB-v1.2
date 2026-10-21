#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
//  Puntero de "mano cerrada" (agarrando), el mismo que usa macOS al arrastrar.
//  JUCE trae la mano abierta (DraggingHandCursor) pero no la cerrada: en Mac se
//  toma la del sistema (ClosedHandCursor.mm); en otros sistemas, la mano abierta.
// ---------------------------------------------------------------------------
#if JUCE_MAC
juce::MouseCursor makeClosedHandCursor();
#else
inline juce::MouseCursor makeClosedHandCursor() { return juce::MouseCursor::DraggingHandCursor; }
#endif

// Se arma una sola vez y se reusa (así también se puede comparar en las pruebas).
inline const juce::MouseCursor& closedHandCursor()
{
    static const juce::MouseCursor cursor = makeClosedHandCursor();
    return cursor;
}
