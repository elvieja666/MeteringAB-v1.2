#pragma once
#include <juce_core/juce_core.h>

// Permite escribir textos con acentos: "Medición"_u
// (sin esto JUCE los muestra mal, porque no sabe que están en UTF-8).
inline juce::String operator""_u (const char* text, size_t length)
{
    return juce::String::fromUTF8 (text, (int) length);
}
