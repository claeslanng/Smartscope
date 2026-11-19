# CLAUDE.md: Smartscope

Smartscope is a tempo-synced oscilloscope VST3 for checking whether kick and bass are
in phase. It's made by Claes Lanng (vendor name **"Claes Lanng Plugins"**) for his own
use in **FL Studio on Windows**, and released free under **AGPLv3**.

The product page is https://claeslanng.shop/smartscope/ (repo `claeslanng/claeslanng.github.io`).

## Decisions the owner made (keep them)

- **VST3 for Windows only.** No standalone, no AU/Mac builds unless asked.
- **One trace: left + right averaged.** No L/R/Mid/Side/Split views.
- **No freeze, no MIDI or audio triggering, no preset browser.** The ShaperBox-style
  extras were left out on purpose.
- **No time measurement tool** (Ctrl+drag between two points). The owner turned it
  down.
- **Tempo sync** to the host's tempo and song position (PPQ), aligned to bar lines.
  Lengths: 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars, 4 bars. When the transport is stopped it
  free-runs at the host tempo while there's signal, and keeps the last picture when the
  signal goes silent.
- **Linear amplitude scale with dB labels**, so polarity is visible. 0 dBFS sits just
  inside the top and bottom edges.
- **Waveform look:** close to ShaperBox. A filled min/max envelope with a thin outline:
  solid where busy, never thinner than a clean line, smooth edges. The owner cares about
  this; don't go back to 1 px bars.
- **Red 0 dB markers** at the top edge where the raw signal goes above 0 dBFS.
- **Display-only low-pass** (zero-phase, run forwards and backwards), which also redraws
  while stopped. It never touches the audio.
- **Hold:** a snapshot drawn as a reference trace.
- **Three optional stereo sidechain inputs**, drawn as coloured layers above the main
  trace (an option puts the main trace on top). Header chips show and hide them.
- **Settings panel (gear button):** theme (Graphite, Midnight, Phosphor, Daylight), a
  colour and opacity per layer and for Hold, display toggles, and About. Everything is
  saved in the project (the `UI` ValueTree child; the theme index is a state property).
- **Branding:** "CLAES LANNG PLUGINS" under the logo, in About, and as COMPANY_NAME.
  Plugin codes: manufacturer `Clng`, plugin `Smsc`. Don't change them, or saved
  projects won't find the plugin.
- **The plugin never changes the audio** (pass-through, zero latency).

## Code map

```
Source/PluginProcessor.*  capture: tempo sync, window buffer per layer (main + 3 sidechains)
Source/ScopeView.*        drawing, grid, zoom/scroll, display low-pass, hold, clip markers
Source/PluginEditor.*     window, header (logo, status, layer chips, ?, gear), footer, look-and-feel
Source/SettingsPanel.*    settings overlay and colour pickers
Source/Themes.h           colour themes
Source/UISettings.h       saved display preferences (colours, opacity, visibility, toggles)
Tests/RenderTest.cpp      offline sync checks with a fake host + PNG renders of the UI
```

The audio thread writes into preallocated buffers (`maxDisplaySamples = 1 << 21` per
layer). The editor reads them on a 60 Hz timer. Keep allocation and locks off the audio
thread.

## Build and check

Linux (cloud sessions). JUCE 8.0.9 comes in through FetchContent:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSMARTSCOPE_BUILD_TESTS=ON
cmake --build build --target RenderTest Smartscope_VST3 -j2
cd /tmp && ./path/to/build/RenderTest_artefacts/Release/RenderTest   # writes render_*.png, prints PASS/FAIL
```

Look at the renders yourself before reporting UI changes, and send the user the relevant
PNGs. RenderTest also writes the README screenshots (`render_docs_*`), which belong in
`docs/images/`.

Windows is only built by GitHub Actions (`.github/workflows/build-windows.yml`). MSVC is
stricter than GCC. A known trap: capturing `this` inside a nested lambda's init-capture.
Capture a `SafePointer` outside instead.

## Releases

Every push builds the Windows VST3 (artifact on the Actions run). To publish a release:
bump `project(Smartscope VERSION x.y.z)` in `CMakeLists.txt`, add a `## [x.y.z]` section
to `CHANGELOG.md`, and push to `main`. The workflow creates the `vX.Y.Z` tag and a GitHub
Release with `Smartscope-vX.Y.Z-Windows-VST3.zip`, using that changelog section as the
notes. Cloud sessions can't push tags, which is why the workflow creates them. After a
release, update the version on the product page in `claeslanng/claeslanng.github.io`.

## Open ideas (not started)

- **v2: inter-instance overlay.** Show another instance's signal (e.g. the kick track)
  behind this one without sidechain routing, like Scyllascope's overlays.
- **More real FL Studio screenshots.** Two real ones are in, added by hand (RenderTest
  doesn't write them): `docs/images/fl-studio.png` (README top image) and
  `docs/images/fullscreen.png` ("Make it big"). The zoom, themes and settings images
  are still synthetic renders.

## Selling points the owner wants promoted

- **Any size** (760 × 360 up to 3000 × 1600, `setResizeLimits` in PluginEditor.cpp):
  detach it in FL and give it a whole second monitor, TV or tablet. Keep the README
  "Make it big" section and the product page's matching section in step with the limits.

## Verified in FL Studio (2026-11-19, v1.1.0)

- All three sidechain inputs show up in FL's Processing → Connections and draw as
  layers. Input and Output on "---" means the track's own audio (the main trace).
- The status reads SYNCED while playing, with the right BPM and window length.
- An empty main trace (Peak -inf) just means the track is silent. It's not a bug.
- To watch: the owner has **Allow smart disable** on. With FL's global Smart Disable,
  FL may stop calling the plugin when its own track is silent, which would freeze the
  sidechain layers too. Not seen yet; it's the first thing to check if a layer stops
  updating while its source plays.
