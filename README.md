# Smartscope

A tempo-synced oscilloscope VST3 for checking whether kicks and basses are in phase.

## Features

- **Tempo sync:** the window locks to your DAW's tempo and song position, so it always starts on the beat and stays still while you loop.
- **Window length:** 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars.
- **One combined trace:** left and right averaged.
- **Linear amplitude scale (labelled in dB):** you can see polarity, so you can tell whether kick and bass swing the same way.
- **Zoom down to single samples:** individual samples show as dots, and the time grid switches to milliseconds when you're zoomed in far.
- **Low-pass for the display only:** shows just the kick and bass. It doesn't touch the audio, adds no phase shift or delay, and also redraws while playback is stopped.
- **Hold:** keeps a blue snapshot to compare against. Nudge the bass, then compare.
- **Hover readout:** time in ms from the start of the window, plus the sample's value, sign and dB.
- **0 dB markers:** red ticks at the top of the display show where the signal goes above 0 dBFS, and the peak readout turns red.
- **Themes:** Graphite, Midnight, Phosphor, Daylight. The choice is saved with the project.
- **Help:** hover over the **?** button for the mouse shortcuts.

The plugin passes audio through unchanged.

## Mouse

| Action | Does |
| --- | --- |
| Wheel | Zoom in/out around the mouse |
| Shift + wheel (or horizontal swipe) | Scroll |
| Ctrl + wheel | Display gain |
| Alt + wheel | Window length |
| Drag | Pan |
| Double-click | Reset zoom |

When the transport is stopped, the scope free-runs at the project tempo while there is signal. When the signal goes silent, it keeps the last picture.

## Getting the plugin (Windows)

Every push builds the plugin on GitHub Actions.

1. Open the repo's **Actions** tab and click the latest green **Build Windows VST3** run.
2. Download **Smartscope-VST3-Windows** under *Artifacts* and unzip it.
3. Copy the `Smartscope.vst3` folder to `C:\Program Files\Common Files\VST3\`.
4. In FL Studio: *Options → Manage plugins → Find more plugins*, then add Smartscope to an effect slot.

## Building locally

```
cmake -B build
cmake --build build --config Release --target Smartscope_VST3
```

JUCE 8 is downloaded automatically. Add `-DSMARTSCOPE_BUILD_TESTS=ON` to build `RenderTest`. It checks the tempo sync with a fake host and renders screenshots of the UI.
