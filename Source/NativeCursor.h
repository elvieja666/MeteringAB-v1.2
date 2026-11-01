#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
//  La mano cerrada nativa de macOS (la misma de Finder, Mapas, etc.).
//  JUCE trae la mano abierta pero no la cerrada: acá se arma un cursor de JUCE con
//  la imagen y el punto de la cerrada de macOS, así JUCE la maneja como cualquier
//  otro cursor (no titila al arrastrar y al soltar vuelve sola la abierta).
//  En otras plataformas (las pruebas en Linux) es la mano de JUCE.
// ---------------------------------------------------------------------------
namespace NativeCursor
{
#if defined (__APPLE__)
    juce::MouseCursor closedHand();
#else
    inline juce::MouseCursor closedHand() { return juce::MouseCursor::DraggingHandCursor; }
#endif
}
