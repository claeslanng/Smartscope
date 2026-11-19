# Changelog

## [Unreleased]

### Changed
- The window now resizes up to 7680 × 4320 (it stopped at 3000 × 1600), so it can fill a 4K or 8K screen or a super-ultrawide, even at 100% display scaling.

## [1.1.0] - 2026-11-18

### Added
- **Sidechain layers:** three optional sidechain inputs, each drawn as its own coloured layer with the same tempo sync as the main input.
- **Layer chips** in the header to show or hide each layer. They also show which inputs are connected.
- **Settings panel** (gear button):
  - Theme.
  - Colour picker and opacity for every layer and for the Hold trace.
  - Display options: hover readout, play cursor, 0 dB markers, main layer on top.
  - An About section.
- **Themes:** Graphite, Midnight, Phosphor, Daylight.
- **0 dB markers:** red ticks where the signal goes above 0 dBFS.
- Claes Lanng Plugins branding, also used as the plugin's vendor name.

### Changed
- Cleaner waveform drawing: solid where the signal is busy, smooth edges, and a minimum line thickness.
- Refined grid with bar, beat and subdivision lines.
- Mouse shortcuts moved into the **?** tooltip. The zoom readout is now the reset button, so nothing overlaps at small window sizes.

## [1.0.0] - 2026-11-18

### Added
- Tempo-synced oscilloscope with window lengths from 1/16 to 4 bars.
- One combined L+R trace on a linear scale with dB labels.
- Zoom down to individual samples, scrolling, display gain, and mouse shortcuts.
- Display-only low-pass that adds no phase shift and also redraws while playback is stopped.
- Hold reference trace, hover readout and peak readout.
