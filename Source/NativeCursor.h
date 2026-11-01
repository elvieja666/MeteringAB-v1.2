#pragma once

// ---------------------------------------------------------------------------
//  Manos nativas de macOS (las mismas de Finder, Mapas, etc.).
//  JUCE trae la mano abierta pero no la cerrada: la cerrada se pide directo a macOS
//  mientras arrastrás, y al soltar se vuelve a poner la abierta.
//  En otras plataformas (las pruebas en Linux) no hacen nada.
// ---------------------------------------------------------------------------
namespace NativeCursor
{
#if defined (__APPLE__)
    void showClosedHand();
    void showOpenHand();
#else
    inline void showClosedHand() {}
    inline void showOpenHand() {}
#endif
}
