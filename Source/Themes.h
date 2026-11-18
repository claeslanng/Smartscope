#pragma once

#include <juce_graphics/juce_graphics.h>

/** Colour sets for the whole plugin. The theme index is saved with the project. */
struct Theme
{
    const char* name;

    juce::Colour window;      // editor background (header / footer)
    juce::Colour scopeBg;     // waveform area
    juce::Colour rulerBg;     // time ruler strip
    juce::Colour control;     // buttons, boxes

    juce::Colour gridBar;     // bar lines, 0 dB lines
    juce::Colour gridBeat;    // beat lines
    juce::Colour gridSub;     // labelled subdivisions, dB lines
    juce::Colour gridMinor;   // unlabelled subdivisions
    juce::Colour centreLine;
    juce::Colour outline;

    juce::Colour text;
    juce::Colour textDim;

    juce::Colour trace;
    juce::Colour hold;
    juce::Colour accent;
    juce::Colour clip;

    float glow;               // 0 = none, otherwise the alpha of a soft halo around the trace
};

inline const Theme themes[] = {
    { "Graphite",
      juce::Colour (0xff1e2023), juce::Colour (0xff161719), juce::Colour (0xff1b1c1f), juce::Colour (0xff141517),
      juce::Colour (0xff3b3e43), juce::Colour (0xff2e3135), juce::Colour (0xff25272b), juce::Colour (0xff1e2023),
      juce::Colour (0xff4a4e54), juce::Colour (0xff2c2f33),
      juce::Colour (0xffb8bcc2), juce::Colour (0xff6f747b),
      juce::Colour (0xffcfd0d2), juce::Colour (0xff3fb6ff), juce::Colour (0xfff08a24), juce::Colour (0xffff3b3b),
      0.0f },

    { "Midnight",
      juce::Colour (0xff141828), juce::Colour (0xff0e1120), juce::Colour (0xff121627), juce::Colour (0xff0c0f1c),
      juce::Colour (0xff353d5e), juce::Colour (0xff2a3150), juce::Colour (0xff20263f), juce::Colour (0xff181d32),
      juce::Colour (0xff3d4670), juce::Colour (0xff262c47),
      juce::Colour (0xffb3bbd8), juce::Colour (0xff666f94),
      juce::Colour (0xffff8a3d), juce::Colour (0xff8fa8ff), juce::Colour (0xff22e39b), juce::Colour (0xffff3d6e),
      0.18f },

    { "Phosphor",
      juce::Colour (0xff0f1611), juce::Colour (0xff080d09), juce::Colour (0xff0c130e), juce::Colour (0xff070b08),
      juce::Colour (0xff24402b), juce::Colour (0xff1b3021), juce::Colour (0xff142619), juce::Colour (0xff101d13),
      juce::Colour (0xff2b5134), juce::Colour (0xff1b2e1f),
      juce::Colour (0xff8fd0a0), juce::Colour (0xff4f7a5a),
      juce::Colour (0xff74ffa0), juce::Colour (0xffffc857), juce::Colour (0xff74ffa0), juce::Colour (0xffff4a4a),
      0.22f },

    { "Daylight",
      juce::Colour (0xffe6e6e2), juce::Colour (0xfff7f7f5), juce::Colour (0xffeeeeeb), juce::Colour (0xfffbfbfa),
      juce::Colour (0xffb9bbbf), juce::Colour (0xffcfd1d4), juce::Colour (0xffdfe0e2), juce::Colour (0xffebebec),
      juce::Colour (0xffa4a7ac), juce::Colour (0xffc9cbce),
      juce::Colour (0xff35383d), juce::Colour (0xff80848b),
      juce::Colour (0xff26282c), juce::Colour (0xff1f7ae0), juce::Colour (0xffe0701a), juce::Colour (0xffe02424),
      0.0f },
};

inline constexpr int numThemes = (int) (sizeof (themes) / sizeof (themes[0]));

inline const Theme& getTheme (int index)
{
    return themes[juce::jlimit (0, numThemes - 1, index)];
}
