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
- **Real screenshots.** The product page now uses real FL Studio captures only (since
  2026-11-19, done in the website conversation): `main.webp`, `zoom.webp`, four
  `theme-*.webp`, `settings.webp`, plus `main-share.jpg` for social previews and a short
  demo video (`drag-loop.mp4`). They're lossless WebP at full size, in
  `assets/img/smartscope/` in the site repo, framed by a 1 px outline instead of soft
  shadows (the owner found those low quality). In this repo, the README's
  `docs/images/fl-studio.png` (top image) and `docs/images/fullscreen.png` ("Make it big")
  are real captures added by hand. `zoom.png`, `themes.png` and `settings.png` are still
  RenderTest renders fed with made-up audio. The next item replaces both kinds.

- **RenderTest renders the website screenshots (after 1.2.0).** The owner doesn't want
  to capture everything by hand for every release. **Don't start this until the 1.2.0
  features are finished** (the owner's call). Full plan:
  `smartscope-website-handoff.md` from the website conversation, and the site repo's
  CLAUDE.md.
  - **Test audio:** on the **`screenshot-audio`** branch, in `Tests/audio/` (not merged;
    merge it first when this starts). Both files are 4 bars at 128 BPM (7.5 s), 44.1 kHz
    stereo, and start on the downbeat at the same sample. A README there says they're
    under the repo's AGPLv3, and the owner knows the repo is public.
    - `Smartscope Audio-Visual Test - Kick 1.wav`: one kick per beat, about 175 ms long,
      16-bit. It goes into **sidechain 1**.
    - `Smartscope Audio-Visual Test - Bass 1.wav`: the bass with sidechain ducking
      already applied, coming back about 4 ms after each kick ends. 24-bit. It goes into
      the **main input**. (This is the second, tighter take.)
  - **How:** load both WAVs in RenderTest and run them through the processor with the
    fake playhead playing at 128 BPM, so the window buffers fill as in FL. Render at
    **2× scale** (`createComponentSnapshot (bounds, true, 2.0f)`) at a window size that
    gives about **2280 × 784 px**, the shape of the current site images. Play cursor
    off; there's no mouse, so there's no hover readout. Layer opacity at 100%.
  - **Keep it in the RenderTest target.** The shipping VST3 gets no hidden mode, and its
    audio path isn't touched.
  - **Output:** lossless PNGs in a folder of their own (e.g. `docs/images/site/`) or as
    an Actions artifact. The website side converts them to WebP and swaps them in.

    | File | Settings |
    |---|---|
    | `site_main.png` | Graphite, length 1/2, playing, SC 1 kick orange, main bass light gray |
    | `site_zoom.png` | The first ~14 ms of a kick, about Zoom x68 |
    | `site_theme_graphite.png` | Orange kick, light gray bass (the defaults) |
    | `site_theme_midnight.png` | Cyan kick (about `#40D0FF`), white bass |
    | `site_theme_phosphor.png` | Amber kick (about `#FFC23D`), phosphor-green bass (about `#6CFF8A`) |
    | `site_theme_daylight.png` | Near-black bass, deep blue (`#2563EB`) or red (`#E5484D`) kick; pale colors get lost on the light background |
    | `site_settings.png` | Settings open, Graphite, **no audio behind it**, 2× so the text is sharp |
    | `site_drag_####.png` | Frames for the demo video (below) |

    The four theme shots use the same audio, view and position, so only the look
    changes. The site names the colors in the captions, so people know the colors are
    chosen separately from the theme.
  - **Demo video frames:** about Zoom x27 on a two-bar window, playback stopped, a
    scripted smooth pan of exactly **one beat** (468.75 ms at 128 BPM). Every beat has the
    same kick, so the last frame matches the first and the video loops without a seam
    (the current one needed a blend). 30 or 60 fps at 2×. The website side encodes a
    muted H.264 MP4.
  - **Page text is tied to the images.** If renders show something different, the site
    text has to change too: the main caption (kick on SC 1 orange over the bass gray,
    half a bar), the zoom text ("first 14 ms… zoomed in 68 times", "the bass (white)
    hasn't started yet"), the video text (x27, two bars, stopped, kick to kick), and the
    features list.
  - **Later, optional:** a GitHub Actions job runs RenderTest on each release and pushes
    the images (and the version number) into `claeslanng/claeslanng.github.io`. It needs a
    token or deploy key with write access there, added by the owner as a repo secret.
  - **Until then, hand captures:** enlarge the window to about 2000 px wide (or Windows
    scaling 200%), press Hold, turn off the play cursor, keep the mouse out, and capture
    just the plugin without FL's frame. Send PNGs attached in the chat, not Gyazo links.
    For video, use the Snipping Tool's video mode at full size, starting and ending on the
    same view.
  - **Same approach later for PitchScope**, whose site images are small 840 px JPEGs.

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
