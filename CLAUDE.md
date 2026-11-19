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
- **Three optional stereo sidechain inputs**, drawn as colored layers above the main
  trace (an option puts the main trace on top). Header chips show and hide them.
- **Settings panel (gear button):** theme (Graphite, Midnight, Phosphor, Daylight), a
  color (swatch + picker, plus a #RRGGBB hex box) and opacity per layer and for Hold,
  display toggles, and About. Everything is saved in the project (the `UI` ValueTree
  child; the theme index is a state property). Needs at least 760 × 352: the editor closes
  it below that and grays out the gear.
- **Every layer defaults to 100% opacity** (`UISettings::defaultOpacity`).
- **Random colors (dice button in Settings), like FL Studio's color dice:** sidechains
  1-3 only, never Main. Hues about a third of the wheel apart (at least 72°), saturated
  but not neon, contrast ≥ 3:1 against the theme's scope background, and clear of the
  Main and Hold hues. `UISettings::randomSidechainColours`; RenderTest checks the rules.
- **Scope only (corners button in the header):** hides header and footer, the scope fills
  the window. A small button in the top-left corner (over the empty bit left of the ruler)
  appears for 2.5 s after the mouse moves and brings everything back. Saved in the
  project (`scopeOnly`). No keyboard shortcut: the scope deliberately never takes keyboard
  focus, so FL keeps its keys.
- **Branding:** "CLAES LANNG PLUGINS" under the logo, in About, and as COMPANY_NAME.
  Plugin codes: manufacturer `Clng`, plugin `Smsc`. Don't change them, or saved
  projects won't find the plugin.
- **The plugin never changes the audio** (pass-through, zero latency).
- **US spelling in anything people read** ("color", "colored", "gray"): UI text, tooltips,
  README, changelog, product page. Code identifiers keep JUCE's `Colour`.

## Code map

```
Source/PluginProcessor.*  capture: tempo sync, window buffer per layer (main + 3 sidechains)
Source/ScopeView.*        drawing, grid, zoom/scroll, display low-pass, hold, clip markers
Source/PluginEditor.*     window, header (logo, status, layer chips, ?, gear), footer, look-and-feel
Source/SettingsPanel.*    settings overlay and color pickers
Source/Themes.h           color themes
Source/UISettings.h       saved display preferences (colors, opacity, visibility, toggles)
Tests/RenderTest.cpp      offline sync checks with a fake host + PNG renders of the UI;
                          `--site <dir>` renders the README/website screenshots
Tests/make_site_images.py turns those renders into README PNGs and website WebP/JPEG/MP4
Tests/audio/              the owner's kick and bass for the screenshots (AGPLv3, see README there)
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
PNGs.

Windows is only built by GitHub Actions (`.github/workflows/build-windows.yml`). MSVC is
stricter than GCC. A known trap: capturing `this` inside a nested lambda's init-capture.
Capture a `SafePointer` outside instead.

## Releases

Every push builds the Windows VST3 (artifact on the Actions run). To publish a release:
bump `project(Smartscope VERSION x.y.z)` in `CMakeLists.txt`, add a `## [x.y.z]` section
to `CHANGELOG.md`, and push to `main`. The workflow creates the `vX.Y.Z` tag and a GitHub
Release with `Smartscope-vX.Y.Z-Windows-VST3.zip`, using that changelog section as the
notes. Cloud sessions can't push tags, which is why the workflow creates them. After a
release, update the version on the product page in `claeslanng/claeslanng.github.io`,
and if the UI changed, rerender the screenshots (see Screenshots).

## Open ideas (not started)

- **v2: inter-instance overlay.** Show another instance's signal (e.g. the kick track)
  behind this one without sidechain routing, like Scyllascope's overlays.
- **Automatic screenshots on release (optional, not set up).** A GitHub Actions job
  could run `RenderTest --site` and `make_site_images.py` on each release and push the
  images (and the version number) into `claeslanng/claeslanng.github.io`. It needs a token
  or deploy key with write access there, added by the owner as a secret in this repo.
- **PitchScope** could get the same rendered screenshots (its site images are small
  840 px JPEGs).

## Screenshots (README and product page)

All of them are rendered, not captured (since 1.2.0), so they never need a hand capture
for a release. After a UI change, rerender and publish both:

```bash
build/RenderTest_artefacts/Release/RenderTest --site /tmp/renders
python3 Tests/make_site_images.py /tmp/renders --readme docs/images \
    --site ../claeslanng.github.io/assets/img/smartscope
```

- **Input:** the owner's real audio in `Tests/audio/`, 4 bars at 128 BPM, 44.1 kHz: the
  kick goes into **sidechain 1**, the ducked bass into the **main input**. The fake playhead
  plays the whole loop, so the buffers fill as in FL. Play cursor off, no mouse (no hover
  readout), layer opacity 100%, 2x scale (`createComponentSnapshot (…, true, 2.0f)`).
  The screenshot mode lives only in RenderTest: the plugin has no hidden mode.
- **Output (`--site`):** `site_main` (Graphite, 1/2 bar, 1140 × 392 → 2280 × 784 px),
  `site_zoom` (first ~14 ms of a kick, Zoom x68), `site_theme_*` (same audio and view;
  Graphite defaults, Midnight white bass + `#40D0FF` kick, Phosphor `#6CFF8A` bass +
  `#FFC23D` kick, Daylight default near-black bass + `#2563EB` kick), `site_settings`
  (just the card, no audio behind it, default display options), and `drag_frames/`
  (192 frames at 30 fps: Zoom x27 on a two-bar window, sliding one beat to the next kick
  and back, eased, with holds).
- **Why the loop goes back and forth:** the ruler labels are positions in the window, so a
  one-way pan of one beat would jump at the seam, even though every kick is identical.
- **README** gets `main.png`, `zoom.png`, `themes.png` (2 × 2 grid) and `settings.png`.
  `fl-studio.png` (two windows in FL) and `fullscreen.png` (a second monitor) are the
  owner's hand captures from 1.1 and stay as they are.
- **Website** gets `main.webp`, `zoom.webp`, `theme-*.webp` (1140 px), `settings.webp`
  (shown at half its pixel size), `main-share.jpg` (1200 px) and `drag-loop.mp4` + poster
  (1280 × 440). The site's captions name what's in the images (layer colors per theme,
  Zoom x68 / 14 ms, x27 / two bars): change them if the renders change.

## Selling points the owner wants promoted

- **Any size** (240 × 110 up to 7680 × 4320, `minWidth`/`maxWidth` etc. in PluginEditor.h):
  detach it in FL (the menu item is called **Detached**, under the arrow at the top left
  of the plugin window) and give it a whole second monitor, TV or tablet, or shrink it to
  a strip. Pairs with **Scope only**. Keep the README "Make it big" section and the
  product page's matching section in step with the limits.
- **Small-window layout:** header drops status, then logo, then chips become dots, then
  `?`. Footer controls have priorities in `layoutFooter` (labels go first, then zoom,
  low-pass slider, gain, < >, low-pass; length box and Hold stay). The footer hides when
  the scope would get under 80 px. RenderTest checks nothing overlaps at 640/480/340/240.

## Verified in FL Studio (2026-11-19, v1.1.0)

- All three sidechain inputs show up in FL's Processing → Connections and draw as
  layers. Input and Output on "---" means the track's own audio (the main trace).
- The status reads SYNCED while playing, with the right BPM and window length.
- An empty main trace (Peak -inf) just means the track is silent. It's not a bug.
- To watch: the owner has **Allow smart disable** on. With FL's global Smart Disable,
  FL may stop calling the plugin when its own track is silent, which would freeze the
  sidechain layers too. Not seen yet; it's the first thing to check if a layer stops
  updating while its source plays.
