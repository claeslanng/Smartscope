#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include "Themes.h"

#include <array>
#include <cmath>

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
    inline const juce::Identifier scopeOnlyKey  { "scopeOnly" };

    inline juce::Colour readColour (const juce::ValueTree& ui, const juce::Identifier& key, juce::Colour fallback)
    {
        const auto s = ui.getProperty (key).toString();
        return s.isEmpty() ? fallback : juce::Colour::fromString (s);
    }

    inline juce::Colour layerColour (const juce::ValueTree& ui, int layer, const Theme& t)
    {
        return readColour (ui, colourKey (layer), defaultLayerColour (layer, t));
    }

    inline constexpr float defaultOpacity = 1.0f;   // every layer starts fully opaque

    inline float layerOpacity (const juce::ValueTree& ui, int layer)
    {
        return juce::jlimit (0.1f, 1.0f, (float) ui.getProperty (opacityKey (layer), defaultOpacity));
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

    //==========================================================================
    // Hex colour codes (#RRGGBB)

    inline juce::String toHex (juce::Colour c)
    {
        return "#" + juce::String::toHexString ((int) (c.getARGB() & 0xffffffu)).paddedLeft ('0', 6).toUpperCase();
    }

    /** Accepts "RRGGBB", "#RRGGBB" and the short "RGB" / "#RGB" form (spaces ignored).
        Returns false and leaves `out` alone if the text isn't a colour. */
    inline bool parseHex (const juce::String& text, juce::Colour& out)
    {
        auto s = text.removeCharacters (" \t").trimCharactersAtStart ("#");
        if (s.length() == 3)
            s = juce::String::charToString (s[0]) + s[0] + s[1] + s[1] + s[2] + s[2];
        if (s.length() != 6 || ! s.containsOnly ("0123456789abcdefABCDEF"))
            return false;
        out = juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) s.getHexValue32()));
        return true;
    }

    //==========================================================================
    // Random colours for the sidechain layers

    /** WCAG contrast ratio between two colours (1 = none, 21 = black on white). */
    inline float contrastRatio (juce::Colour a, juce::Colour b)
    {
        auto lum = [] (juce::Colour c)
        {
            auto ch = [] (juce::uint8 v)
            {
                const float x = (float) v / 255.0f;
                return x <= 0.03928f ? x / 12.92f : std::pow ((x + 0.055f) / 1.055f, 2.4f);
            };
            return 0.2126f * ch (c.getRed()) + 0.7152f * ch (c.getGreen()) + 0.0722f * ch (c.getBlue());
        };
        const float la = lum (a), lb = lum (b);
        return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
    }

    /** Distance between two hues on the colour wheel (0..0.5, where 0.5 = opposite). */
    inline float hueDistance (float a, float b)
    {
        const float d = std::abs (a - b);
        return juce::jmin (d, 1.0f - d);
    }

    inline constexpr float minRandomContrast = 3.0f;    // against the scope background
    inline constexpr float minRandomHueGap   = 0.2f;    // between the three layers (72 degrees)
    inline constexpr float minAvoidHueGap    = 0.08f;   // from the Main and Hold colours (29 degrees)

    /** Three colours for sidechains 1-3, like FL Studio's dice: spread around the colour
        wheel, saturated but not neon, bright enough to read on the theme's scope background,
        and kept clear of the hues of `avoidA` / `avoidB` (Main and Hold) if those have a hue. */
    inline std::array<juce::Colour, 3> randomSidechainColours (const Theme& t, juce::Random& rng,
                                                               juce::Colour avoidA, juce::Colour avoidB)
    {
        const bool lightBackground = t.scopeBg.getPerceivedBrightness() > 0.5f;

        float avoid[2] {};
        int numAvoid = 0;
        for (auto c : { avoidA, avoidB })
            if (c.getSaturation() > 0.25f)
                avoid[numAvoid++] = c.getHue();

        for (int attempt = 0; attempt < 500; ++attempt)
        {
            std::array<juce::Colour, 3> out;
            const float base = rng.nextFloat();
            bool ok = true;

            for (int i = 0; i < 3 && ok; ++i)
            {
                // Roughly a third of the wheel apart, with some wobble so it doesn't look mechanical
                const float h = std::fmod (base + (float) i / 3.0f + (rng.nextFloat() - 0.5f) * 0.12f + 1.0f, 1.0f);
                const float s = 0.55f + rng.nextFloat() * 0.30f;
                const float l = lightBackground ? 0.34f + rng.nextFloat() * 0.12f
                                                : 0.58f + rng.nextFloat() * 0.12f;
                out[(size_t) i] = juce::Colour::fromHSL (h, s, l, 1.0f);

                ok = contrastRatio (out[(size_t) i], t.scopeBg) >= minRandomContrast;
                for (int k = 0; k < numAvoid && ok; ++k)
                    ok = hueDistance (h, avoid[k]) >= minAvoidHueGap;
            }

            if (ok)
            {
                // Shuffle so SC 1 isn't always the "first" hue
                for (int i = 2; i > 0; --i)
                    std::swap (out[(size_t) i], out[(size_t) rng.nextInt (i + 1)]);
                return out;
            }
        }

        return { defaultLayerColour (1, t), defaultLayerColour (2, t), defaultLayerColour (3, t) };
    }
}
