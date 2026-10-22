#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
//  Puntero de "mano cerrada" (agarrando), para arrastrar con zoom.
//  JUCE trae la mano abierta (DraggingHandCursor) pero no la cerrada, así que se
//  dibuja acá (puño blanco con borde negro, como el de macOS), a 3x para que se
//  vea nítido en pantallas Retina. (Tomar la imagen del sistema dejaba el puntero
//  invisible en algunas Mac.)
// ---------------------------------------------------------------------------
namespace ClosedHand
{
    constexpr int size = 18;                  // en puntos (como los punteros de macOS)

    // Forma del puño en un cuadro de 18 x 18 puntos.
    inline juce::Path shape()
    {
        juce::Path p;
        p.addRoundedRectangle (3.6f, 7.0f, 11.2f, 8.4f, 3.0f);                // palma
        p.addRoundedRectangle (2.2f, 8.2f, 3.6f, 4.4f, 1.6f);                 // pulgar
        for (int k = 0; k < 4; ++k)                                           // nudillos (dedos doblados)
            p.addRoundedRectangle (4.6f + 2.45f * (float) k, 5.0f, 2.35f, 4.2f, 1.15f);
        return p;
    }

    inline juce::Image image (float scale)
    {
        const int px = juce::roundToInt ((float) size * scale);
        juce::Image img (juce::Image::ARGB, px, px, true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        const auto p = shape();

        // Borde: cada parte con un trazo negro, y encima todo relleno blanco: queda solo el
        // contorno de afuera y las rayitas entre los dedos.
        juce::Path outline;
        juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (outline, p);
        g.setColour (juce::Colours::black);
        g.fillPath (outline);
        g.setColour (juce::Colours::white);
        g.fillPath (p);
        // Rayitas entre los nudillos (los dedos se distinguen).
        g.setColour (juce::Colours::black);
        for (int k = 1; k < 4; ++k)
        {
            const float x = 4.6f + 2.45f * (float) k - 0.05f;
            g.fillRect (juce::Rectangle<float> (x - 0.35f, 5.6f, 0.7f, 2.6f));
        }
        return img;
    }
}

inline juce::MouseCursor makeClosedHandCursor()
{
    constexpr float scale = 3.0f;
    return juce::MouseCursor (juce::ScaledImage (ClosedHand::image (scale), (double) scale),
                              { ClosedHand::size / 2, ClosedHand::size / 2 });
}

// Se arma una sola vez y se reusa (así también se puede comparar en las pruebas).
inline const juce::MouseCursor& closedHandCursor()
{
    static const juce::MouseCursor cursor = makeClosedHandCursor();
    return cursor;
}
