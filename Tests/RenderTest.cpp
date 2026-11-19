// Offline test: feeds a synthetic kick + bass through the processor with a fake
// host playhead, checks the tempo sync, and renders the editor to PNG files.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UISettings.h"

#include <cmath>
#include <iostream>

struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 128.0, ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

// Kick on every beat, bass note starting `bassDelayMs` after the kick
static void render (SmartscopeAudioProcessor& p, FakePlayHead& ph, double sr, double seconds,
                    double bassDelayMs, float bassPolarity, double startPpq = 0.0)
{
    const int block = 441; // odd size on purpose, so blocks don't line up with beats
    const double spb = sr * 60.0 / ph.bpm;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;

    const long total = (long) (seconds * sr);
    const long startSample = (long) std::llround (startPpq * spb);
    double kickPhase = 0.0;

    for (long s0 = 0; s0 < total; s0 += block)
    {
        const int n = (int) std::min<long> (block, total - s0);
        buf.setSize (2, n, false, false, true);
        ph.ppq = (double) (s0 + startSample) / spb;

        for (int i = 0; i < n; ++i)
        {
            const long s = s0 + startSample + i;
            const double tInBeat = std::fmod ((double) s, spb) / sr;

            // Kick: pitch sweep 160 -> 48 Hz with a decaying envelope, restarts each beat
            if (std::fmod ((double) s, spb) < 1.0) kickPhase = 0.0;
            const double f = 48.0 + 112.0 * std::exp (-tInBeat * 30.0);
            kickPhase += juce::MathConstants<double>::twoPi * f / sr;
            const double kick = std::sin (kickPhase) * std::exp (-tInBeat * 9.0);

            // Bass: 48 Hz sine with a soft attack
            const double tb = tInBeat - bassDelayMs / 1000.0;
            double bass = 0.0;
            if (tb > 0.0)
                bass = bassPolarity * 0.5 * std::sin (juce::MathConstants<double>::twoPi * 48.0 * tb)
                       * (1.0 - std::exp (-tb * 200.0)) * std::exp (-tb * 3.0);

            // A bit of hi-hat-ish noise so the low-pass has something to remove
            const double hat = 0.08 * (juce::Random::getSystemRandom().nextFloat() * 2.0 - 1.0)
                               * std::exp (-std::fmod (tInBeat, 0.5 * spb / sr) * 60.0);

            const float v = (float) (0.8 * kick + bass + hat);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        p.processBlock (buf, midi);
    }
}

// Dense mix: kick on every beat, sustained saw-ish bass ducked by the kick, hats, one overshoot
static void renderDense (SmartscopeAudioProcessor& p, FakePlayHead& ph, double sr, double seconds)
{
    const int block = 512;
    const double spb = sr * 60.0 / ph.bpm;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const long total = (long) (seconds * sr);
    double kickPhase = 0.0, bassPhase = 0.0;
    juce::Random rng (42);

    for (long s0 = 0; s0 < total; s0 += block)
    {
        const int n = (int) std::min<long> (block, total - s0);
        buf.setSize (2, n, false, false, true);
        ph.ppq = (double) s0 / spb;
        for (int i = 0; i < n; ++i)
        {
            const long s = s0 + i;
            const double tInBeat = std::fmod ((double) s, spb) / sr;
            const int beat = (int) ((double) s / spb);
            if (std::fmod ((double) s, spb) < 1.0) kickPhase = 0.0;
            const double f = 50.0 + 130.0 * std::exp (-tInBeat * 35.0);
            kickPhase += juce::MathConstants<double>::twoPi * f / sr;
            const double kick = std::sin (kickPhase) * std::exp (-tInBeat * 7.0);

            const double bassF = (beat % 4 == 2) ? 49.0 : 55.0;
            bassPhase += juce::MathConstants<double>::twoPi * bassF / sr;
            double saw = 0.0;
            for (int h = 1; h <= 6; ++h) saw += std::sin (bassPhase * h) / h;
            const double duck = 1.0 - 0.95 * std::exp (-tInBeat * 6.0);
            const double bass = 0.55 * saw * duck;

            const double hat = 0.06 * (rng.nextFloat() * 2.0 - 1.0) * std::exp (-std::fmod (tInBeat, spb / sr / 2.0) * 50.0);
            double v = 0.85 * kick + bass + hat;
            if (beat % 4 == 1 && tInBeat > 0.0 && tInBeat < 0.004) v *= 1.25; // a transient that overshoots 0 dBFS
            buf.setSample (0, i, (float) v);
            buf.setSample (1, i, (float) v);
        }
        p.processBlock (buf, midi);
    }
}

// Bass on the main input, kick on sidechain 1 (bus 1)
static void renderLayers (SmartscopeAudioProcessor& p, FakePlayHead& ph, double sr, double seconds, double bassDelayMs)
{
    const int block = 480;
    const double spb = sr * 60.0 / ph.bpm;
    const int numCh = p.getTotalNumInputChannels();
    juce::AudioBuffer<float> buf (numCh, block);
    juce::MidiBuffer midi;
    const long total = (long) (seconds * sr);
    double kickPhase = 0.0;

    for (long s0 = 0; s0 < total; s0 += block)
    {
        const int n = (int) std::min<long> (block, total - s0);
        buf.setSize (numCh, n, false, false, true);
        buf.clear();
        ph.ppq = (double) s0 / spb;
        for (int i = 0; i < n; ++i)
        {
            const long s = s0 + i;
            const double tInBeat = std::fmod ((double) s, spb) / sr;
            if (std::fmod ((double) s, spb) < 1.0) kickPhase = 0.0;
            const double f = 50.0 + 130.0 * std::exp (-tInBeat * 35.0);
            kickPhase += juce::MathConstants<double>::twoPi * f / sr;
            const double kick = 0.9 * std::sin (kickPhase) * std::exp (-tInBeat * 7.0);

            const double tb = tInBeat - bassDelayMs / 1000.0;
            double bass = 0.0;
            if (tb > 0.0)
            {
                double saw = 0.0;
                for (int h = 1; h <= 5; ++h) saw += std::sin (juce::MathConstants<double>::twoPi * 52.0 * h * tb) / h;
                bass = 0.5 * saw * (1.0 - std::exp (-tb * 150.0)) * std::exp (-tb * 2.0);
            }
            buf.setSample (0, i, (float) bass);
            buf.setSample (1, i, (float) bass);
            if (numCh >= 4)
            {
                buf.setSample (2, i, (float) kick);
                buf.setSample (3, i, (float) kick);
            }
        }
        p.processBlock (buf, midi);
    }
}

static void snapshot (juce::Component& c, const juce::String& name)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("render_" + name + ".png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::cout << "  wrote " << out.getFullPathName() << std::endl;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    const double sr = 48000.0;
    SmartscopeAudioProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);

    std::cout << "Sync checks" << std::endl;
    // 1/2 note window (default) at 128 BPM = 2 beats
    render (proc, ph, sr, 4.0, 12.0, 1.0f);
    const double spb = sr * 60.0 / ph.bpm;
    check (proc.getWindowSamples() == (int) std::lround (2.0 * spb), "window = 2 beats at 128 BPM");
    check (std::abs (proc.getWindowQuarterNotes() - 2.0) < 1e-9, "window = 2 quarter notes");
    check (proc.isSynced(), "reports synced while playing");

    // Kick onset should sit at index 0 and at index spb (beat 2)
    const float* d = proc.getDisplayData();
    auto firstNonZeroAfter = [&] (int start)
    {
        for (int i = start; i < start + 200; ++i)
            if (std::abs (d[i]) > 0.02f)
                return i - start;
        return -1;
    };
    // The kick starts at sin(0) = 0 and rises, so look for the first clearly non-zero sample
    const int off0 = firstNonZeroAfter (0);
    const int off1 = firstNonZeroAfter ((int) std::lround (spb));
    check (off0 >= 0 && off0 < 4, "kick lands at the start of the window (offset " + juce::String (off0) + ")");
    check (off1 >= 0 && off1 < 4, "kick on beat 2 lands one beat in (offset " + juce::String (off1) + ")");

    // Length change: 1 bar
    if (auto* lp = proc.apvts.getParameter (ParamIDs::length))
        lp->setValueNotifyingHost (lp->convertTo0to1 (4.0f));
    render (proc, ph, sr, 0.05, 12.0, 1.0f);
    check (proc.getWindowSamples() == (int) std::lround (4.0 * spb), "1 bar window = 4 beats");
    if (auto* lp = proc.apvts.getParameter (ParamIDs::length))
        lp->setValueNotifyingHost (lp->convertTo0to1 ((float) defaultLengthIndex));

    // Stopped transport + silence keeps the picture
    render (proc, ph, sr, 4.0, 12.0, 1.0f);
    juce::HeapBlock<float> before ((size_t) proc.getWindowSamples());
    std::memcpy (before, d, sizeof (float) * (size_t) proc.getWindowSamples());
    {
        ph.playing = false;
        juce::AudioBuffer<float> silent (2, 512);
        silent.clear();
        juce::MidiBuffer m;
        for (int i = 0; i < 50; ++i) proc.processBlock (silent, m);
        ph.playing = true;
    }
    check (std::memcmp (before, d, sizeof (float) * (size_t) proc.getWindowSamples()) == 0,
           "silence while stopped keeps the last picture");

    // Re-fill with a fresh render (bass slightly late), so the editor has something to show
    render (proc, ph, sr, 4.0, 12.0, 1.0f);

    std::cout << "Rendering" << std::endl;
    std::unique_ptr<SmartscopeAudioProcessorEditor> ed (
        dynamic_cast<SmartscopeAudioProcessorEditor*> (proc.createEditor()));
    ed->setSize (1000, 500);
    auto& scope = ed->getScopeView();

    scope.refreshForTest();
    snapshot (*ed, "1_full");

    scope.setViewForTest (0.0, 0.12);
    scope.setHoverForTest ({ 300.0f, 200.0f });
    scope.refreshForTest();
    snapshot (*ed, "2_zoom_kick");

    proc.apvts.getParameter (ParamIDs::lowpassOn)->setValueNotifyingHost (1.0f);
    scope.refreshForTest();
    snapshot (*ed, "3_zoom_lowpass");

    // Hold the current picture, then flip the bass polarity: the held (blue) trace should differ
    scope.setHold (true);
    render (proc, ph, sr, 4.0, 12.0, -1.0f);
    scope.refreshForTest();
    snapshot (*ed, "4_hold_polarity");

    scope.setHold (false);
    proc.apvts.getParameter (ParamIDs::lowpassOn)->setValueNotifyingHost (0.0f);
    scope.setViewForTest (0.0, 0.0006);
    scope.refreshForTest();
    snapshot (*ed, "5_sample_level");

    if (auto* gp = proc.apvts.getParameter (ParamIDs::gain))
        gp->setValueNotifyingHost (gp->convertTo0to1 (12.0f));
    scope.setViewForTest (0.0, 1.0);
    scope.refreshForTest();
    snapshot (*ed, "6_gain12");

    // Dense 1-bar view at roughly the size of the user's window, in each theme
    if (auto* gp = proc.apvts.getParameter (ParamIDs::gain))
        gp->setValueNotifyingHost (gp->convertTo0to1 (0.0f));
    if (auto* lp = proc.apvts.getParameter (ParamIDs::length))
        lp->setValueNotifyingHost (lp->convertTo0to1 (4.0f));
    renderDense (proc, ph, sr, 2.2);
    ed->setSize (780, 360);
    for (int t = 0; t < numThemes; ++t)
    {
        ed->applyTheme (t);
        scope.refreshForTest();
        snapshot (*ed, "7_dense_theme" + juce::String (t));
    }
    ed->applyTheme (0);
    scope.setViewForTest (0.2, 0.1);
    scope.refreshForTest();
    snapshot (*ed, "8_dense_zoom");

    ed.reset();
    proc.setPlayHead (nullptr);

    //--------------------------------------------------------------------------
    std::cout << "Sidechain layers" << std::endl;
    {
        SmartscopeAudioProcessor p2;
        auto layout = p2.getBusesLayout();
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        check (p2.setBusesLayout (layout), "stereo sidechain 1 layout accepted");
        FakePlayHead ph2;
        p2.setPlayHead (&ph2);
        p2.setRateAndBufferSizeDetails (sr, 512);
        p2.prepareToPlay (sr, 512);
        check (p2.getTotalNumInputChannels() == 4, "4 input channels with sidechain 1 enabled");

        renderLayers (p2, ph2, sr, 3.0, 20.0);
        check (p2.isLayerConnected (1) && ! p2.isLayerConnected (2), "sidechain 1 connected, 2 not");

        const float* k = p2.getDisplayData (1);
        int kOff = -1;
        for (int i = 0; i < 200; ++i) if (std::abs (k[i]) > 0.02f) { kOff = i; break; }
        check (kOff >= 0 && kOff < 4, "kick on sidechain lands at window start (offset " + juce::String (kOff) + ")");

        const float* m = p2.getDisplayData (0);
        int bOff = -1;
        for (int i = 0; i < 4000; ++i) if (std::abs (m[i]) > 0.02f) { bOff = i; break; }
        const int expected = (int) std::lround (0.020 * sr);
        check (bOff >= expected && bOff < expected + 60, "bass on main starts ~20 ms in (sample " + juce::String (bOff) + ")");

        std::unique_ptr<SmartscopeAudioProcessorEditor> e2 (
            dynamic_cast<SmartscopeAudioProcessorEditor*> (p2.createEditor()));
        e2->setSize (900, 420);
        auto& sv = e2->getScopeView();
        sv.refreshForTest();
        snapshot (*e2, "9_layers_full");
        sv.setViewForTest (0.0, 0.08);
        sv.refreshForTest();
        snapshot (*e2, "10_layers_zoom");

        p2.getUISettings().setProperty (UISettings::colourKey (1), juce::Colour (0xffffd23f).toString(), nullptr);
        p2.getUISettings().setProperty (UISettings::opacityKey (0), 0.6, nullptr);
        sv.refreshForTest();
        snapshot (*e2, "11_layers_custom_colour");

        e2->toggleSettings();
        snapshot (*e2, "12_settings");
        e2->setSize (760, 360);
        snapshot (*e2, "13_settings_min");
        e2->toggleSettings();

        // Big screens: the old 3000 x 1600 cap must be gone, and a 4K window must draw
        if (auto* c = e2->getConstrainer())
            check (c->getMaximumWidth() >= 7680 && c->getMaximumHeight() >= 4320,
                   "resize limit reaches 7680 x 4320");
        e2->setSize (3840, 2160);
        check (e2->getWidth() == 3840 && e2->getHeight() == 2160, "editor can be 3840 x 2160");
        sv.refreshForTest();
        snapshot (*e2, "14_layers_4k");

        e2.reset();

        // Images for the README
        if (auto* lp = p2.apvts.getParameter (ParamIDs::length))
            lp->setValueNotifyingHost (lp->convertTo0to1 (4.0f));
        p2.getUISettings().removeProperty (UISettings::colourKey (1), nullptr);
        p2.getUISettings().removeProperty (UISettings::opacityKey (0), nullptr);
        renderLayers (p2, ph2, sr, 2.5, 18.0);
        std::unique_ptr<SmartscopeAudioProcessorEditor> e3 (
            dynamic_cast<SmartscopeAudioProcessorEditor*> (p2.createEditor()));
        e3->setSize (1000, 460);
        e3->getScopeView().refreshForTest();
        snapshot (*e3, "docs_hero");
        e3->getScopeView().setViewForTest (0.0, 0.07);
        e3->getScopeView().refreshForTest();
        snapshot (*e3, "docs_zoom");
        e3->getScopeView().resetZoom();
        e3->setSize (900, 420);
        e3->toggleSettings();
        snapshot (*e3, "docs_settings");
        e3.reset();
        p2.setPlayHead (nullptr);
    }

    std::cout << (failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
