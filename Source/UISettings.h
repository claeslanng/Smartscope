#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include "Themes.h"

/** Display preferences stored in the processor's "UI" ValueTree (saved with the project).
    Message thread only. */
namespace UISettings
{
    inline const char* layerName (int layer)
    {
        static const char* names[] = { "Main", "SC 1", "SC 2", "SC 3" };
        return names[juce::jlimit (0, 3, layer)];
    }

    inline const char* layerLongName (int layer)
    {
        static const char* names[] = { "Main input", "Sidechain 1", "Sidechain 2", "Sidechain 3" };
        return names[juce::jlimit (0, 3, layer)];
    }

    inline juce::Colour defaultLayerColour (int layer, const Theme& t)
    {
        switch (layer)
        {
            case 1:  return juce::Colour (0xffff7a45);   // coral
            case 2:  return juce::Colour (0xff3fd2ff);   // cyan
            case 3:  return juce::Colour (0xffc58bff);   // violet
            default: return t.trace;
        }
    }

    inline juce::Identifier colourKey (int layer)  { return juce::Identifier ("colour"  + juce::String (layer)); }
    inline juce::Identifier opacityKey (int layer) { return juce::Identifier ("opacity" + juce::String (layer)); }
    inline juce::Identifier visibleKey (int layer) { return juce::Identifier ("visible" + juce::String (layer)); }
    inline const juce::Identifier holdColourKey { "holdColour" };
    inline const juce::Identifier showHoverKey  { "showHover" };
    inline const juce::Identifier showCursorKey { "showCursor" };
    inline const juce::Identifier showClipKey   { "showClip" };
    inline const juce::Identifier mainOnTopKey  { "mainOnTop" };

    inline juce::Colour readColour (const juce::ValueTree& ui, const juce::Identifier& key, juce::Colour fallback)
    {
        const auto s = ui.getProperty (key).toString();
        return s.isEmpty() ? fallback : juce::Colour::fromString (s);
    }

    inline juce::Colour layerColour (const juce::ValueTree& ui, int layer, const Theme& t)
    {
        return readColour (ui, colourKey (layer), defaultLayerColour (layer, t));
    }

    inline float layerOpacity (const juce::ValueTree& ui, int layer)
    {
        return juce::jlimit (0.1f, 1.0f, (float) ui.getProperty (opacityKey (layer), layer == 0 ? 1.0f : 0.85f));
    }

    inline bool layerVisible (const juce::ValueTree& ui, int layer)
    {
        return (bool) ui.getProperty (visibleKey (layer), true);
    }

    inline juce::Colour holdColour (const juce::ValueTree& ui, const Theme& t)
    {
        return readColour (ui, holdColourKey, t.hold);
    }

    inline bool flag (const juce::ValueTree& ui, const juce::Identifier& key, bool fallback)
    {
        return (bool) ui.getProperty (key, fallback);
    }
}
