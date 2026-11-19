// Offline test: feeds a synthetic kick + bass through the processor with a fake
// host playhead, checks the tempo sync, and renders the editor to PNG files.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UISettings.h"

#include <cmath>
#include <iostream>
#include <optional>

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
            if (numCh >= 8)
            {
                // Clap on beats 2 and 4 (sidechain 2), a short perc hit on the off-beats (sidechain 3)
                const double tClap = std::fmod ((double) s + spb, 2.0 * spb) / sr;
                const double clap = tClap < 0.25 ? 0.55 * (juce::Random::getSystemRandom().nextFloat() * 2.0 - 1.0)
                                                       * std::exp (-tClap * 28.0) : 0.0;
                const double tPerc = std::fmod ((double) s + 0.5 * spb, spb) / sr;
                const double perc = 0.45 * std::sin (juce::MathConstants<double>::twoPi * 420.0 * tPerc)
                                    * std::exp (-tPerc * 70.0);
                buf.setSample (4, i, (float) clap);
                buf.setSample (5, i, (float) clap);
                buf.setSample (6, i, (float) perc);
                buf.setSample (7, i, (float) perc);
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

//==============================================================================
// Screenshot mode: RenderTest --site <folder>
//
// Renders the images for the README and the product page from the real test audio in
// Tests/audio (kick into sidechain 1, ducked bass into the main input, 4 bars at 128 BPM),
// at 2x scale. Tests/make_site_images.py turns the PNGs into the files the README and the
// website use. Lives only in this test program: the plugin has no hidden mode.

namespace site
{
    constexpr double bpm = 128.0;
    constexpr float  scale = 2.0f;                 // 2x for sharp images on high-density screens
    constexpr int    width = 1140, height = 392;   // 2280 x 784 px at 2x
    constexpr int    lengthHalfBar = 3, lengthOneBar = 4, lengthTwoBars = 5;

    struct Audio
    {
        juce::AudioBuffer<float> kick, bass;
        double sampleRate = 44100.0;
    };

    static bool loadWav (const juce::File& file, juce::AudioBuffer<float>& out, double& sampleRate)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return false;
        out.setSize (2, (int) reader->lengthInSamples);
        reader->read (&out, 0, (int) reader->lengthInSamples, 0, true, true);
        sampleRate = reader->sampleRate;
        return true;
    }

    /** A processor with sidechain 1 on, fed the whole 4-bar loop while "playing" at 128 BPM.
        Stopping exactly at the end of bar 4 leaves the last full window in the buffer. */
    static std::unique_ptr<SmartscopeAudioProcessor> makeProcessor (const Audio& a, FakePlayHead& ph, int lengthIndex,
                                                                    bool withAudio = true, int kickSidechain = 1)
    {
        auto p = std::make_unique<SmartscopeAudioProcessor>();
        auto layout = p->getBusesLayout();
        for (int bus = 1; bus <= kickSidechain; ++bus)   // sidechains up to the kick's are connected
            layout.inputBuses.getReference (bus) = juce::AudioChannelSet::stereo();
        p->setBusesLayout (layout);
        p->setPlayHead (&ph);
        constexpr int block = 512;
        p->setRateAndBufferSizeDetails (a.sampleRate, block);
        p->prepareToPlay (a.sampleRate, block);
        if (auto* lp = p->apvts.getParameter (ParamIDs::length))
            lp->setValueNotifyingHost (lp->convertTo0to1 ((float) lengthIndex));
        p->getUISettings().setProperty (UISettings::showCursorKey, false, nullptr);   // play cursor off

        ph.bpm = bpm;
        ph.playing = true;
        const double spb = a.sampleRate * 60.0 / bpm;
        const int total = withAudio ? a.bass.getNumSamples() : block;
        const int channels = 2 + 2 * kickSidechain;
        juce::AudioBuffer<float> buf (channels, block);
        juce::MidiBuffer midi;
        for (int s0 = 0; s0 < total; s0 += block)
        {
            const int n = juce::jmin (block, total - s0);
            buf.setSize (channels, n, false, false, true);
            buf.clear();
            if (withAudio)
                for (int ch = 0; ch < 2; ++ch)
                {
                    buf.copyFrom (ch,     0, a.bass, ch, s0, n);   // main input
                    buf.copyFrom (ch + 2 * kickSidechain, 0, a.kick, ch, s0, n);   // the kick's sidechain
                }
            ph.ppq = (double) s0 / spb;
            p->processBlock (buf, midi);
        }
        return p;
    }

    static std::unique_ptr<SmartscopeAudioProcessorEditor> makeEditor (SmartscopeAudioProcessor& p, int w, int h)
    {
        std::unique_ptr<SmartscopeAudioProcessorEditor> e (dynamic_cast<SmartscopeAudioProcessorEditor*> (p.createEditor()));
        e->setSize (w, h);
        e->getScopeView().refreshForTest();
        return e;
    }

    static void save (juce::Component& c, juce::Rectangle<int> area, const juce::File& file)
    {
        auto img = c.createComponentSnapshot (area, true, scale);
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    static void setColours (SmartscopeAudioProcessor& p, std::optional<juce::Colour> main, std::optional<juce::Colour> kick)
    {
        auto ui = p.getUISettings();
        if (main) ui.setProperty (UISettings::colourKey (0), main->toString(), nullptr); else ui.removeProperty (UISettings::colourKey (0), nullptr);
        if (kick) ui.setProperty (UISettings::colourKey (1), kick->toString(), nullptr); else ui.removeProperty (UISettings::colourKey (1), nullptr);
    }

    /** The mouse pointer for the demo video: Windows' arrow when the button is up, a grabbing
        hand (what Smartscope shows while dragging) when it's down. Drawn here, at `pos`
        (the arrow's tip, or the middle of the hand), in editor coordinates. */
    enum class Pointer { arrow, grab, resize };

    static void drawPointer (juce::Graphics& g, juce::Point<float> pos, Pointer kind);

    static void drawPointer (juce::Graphics& g, juce::Point<float> pos, bool grabbing)
    {
        drawPointer (g, pos, grabbing ? Pointer::grab : Pointer::arrow);
    }

    static void drawPointer (juce::Graphics& g, juce::Point<float> pos, Pointer kind)
    {
        const bool grabbing = kind == Pointer::grab;
        constexpr float size = 1.5f;   // a bit bigger than 1:1, so it reads in a small video
        const auto ink = juce::Colour (0xff101113), paper = juce::Colours::white;

        if (kind == Pointer::resize)
        {
            // Windows' diagonal resize pointer: a double-headed arrow from top left to bottom
            // right, drawn level and then turned 45 degrees
            juce::Path p;
            const float L = 8.0f, hl = 4.2f, hw = 4.0f, w = 1.3f;   // half length, head length, head and shaft half-widths
            p.startNewSubPath (-L, 0.0f);
            p.lineTo (-L + hl, -hw);
            p.lineTo (-L + hl, -w);
            p.lineTo (L - hl, -w);
            p.lineTo (L - hl, -hw);
            p.lineTo (L, 0.0f);
            p.lineTo (L - hl, hw);
            p.lineTo (L - hl, w);
            p.lineTo (-L + hl, w);
            p.lineTo (-L + hl, hw);
            p.closeSubPath();
            p.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f));
            p.applyTransform (juce::AffineTransform::scale (size).translated (pos));
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillPath (p, juce::AffineTransform::translation (1.0f, 1.5f));
            g.setColour (paper);
            g.fillPath (p);
            g.setColour (ink);
            g.strokePath (p, juce::PathStrokeType (1.1f, juce::PathStrokeType::mitered));
            return;
        }

        if (! grabbing)
        {
            juce::Path arrow;
            arrow.startNewSubPath (0.0f, 0.0f);
            arrow.lineTo (0.0f, 17.0f);
            arrow.lineTo (4.0f, 13.2f);
            arrow.lineTo (7.0f, 19.8f);
            arrow.lineTo (9.6f, 18.7f);
            arrow.lineTo (6.7f, 12.2f);
            arrow.lineTo (12.2f, 12.2f);
            arrow.closeSubPath();
            arrow.applyTransform (juce::AffineTransform::scale (size).translated (pos));
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillPath (arrow, juce::AffineTransform::translation (1.0f, 1.5f));   // soft drop shadow, like Windows
            g.setColour (paper);
            g.fillPath (arrow);
            g.setColour (ink);
            g.strokePath (arrow, juce::PathStrokeType (1.1f, juce::PathStrokeType::mitered));
            return;
        }

        // Grabbing hand: four curled fingers over the palm, thumb tucked in on the left.
        // Outline everything thick first, then fill white on top, so only the outer edge shows.
        juce::Path hand;
        hand.addRoundedRectangle (2.0f, 6.5f, 14.5f, 11.0f, 4.0f);                 // palm
        for (int i = 0; i < 4; ++i)
            hand.addRoundedRectangle (2.6f + 3.45f * (float) i, 3.0f, 3.6f, 7.0f, 1.8f);   // knuckles
        hand.addRoundedRectangle (-0.6f, 8.6f, 6.0f, 4.2f, 2.1f);                  // thumb
        const auto place = juce::AffineTransform::translation (-8.5f, -10.0f).scaled (size).translated (pos);
        hand.applyTransform (place);

        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillPath (hand, juce::AffineTransform::translation (1.0f, 1.5f));
        g.setColour (ink);
        g.strokePath (hand, juce::PathStrokeType (2.2f));
        g.setColour (paper);
        g.fillPath (hand);

        // Creases between the fingers
        g.setColour (ink.withAlpha (0.55f));
        for (int i = 1; i < 4; ++i)
        {
            const float fx = 2.6f + 3.45f * (float) i - 0.1f;
            juce::Line<float> crease (fx, 4.6f, fx, 8.6f);
            crease.applyTransform (place);
            g.drawLine (crease, 0.9f);
        }
    }

    static int render (const juce::File& outDir)
    {
        const auto audioDir = juce::File (SMARTSCOPE_SOURCE_DIR).getChildFile ("Tests/audio");
        Audio a;
        double kickRate = 0.0;
        if (! loadWav (audioDir.getChildFile ("Smartscope Audio-Visual Test - Kick 1.wav"), a.kick, kickRate)
            || ! loadWav (audioDir.getChildFile ("Smartscope Audio-Visual Test - Bass 1.wav"), a.bass, a.sampleRate)
            || kickRate != a.sampleRate || a.kick.getNumSamples() != a.bass.getNumSamples())
        {
            std::cout << "Couldn't load matching test audio from " << audioDir.getFullPathName() << std::endl;
            return 1;
        }
        outDir.createDirectory();
        auto file = [&] (const juce::String& name) { return outDir.getChildFile (name + ".png"); };
        std::cout << "Rendering site images to " << outDir.getFullPathName() << std::endl;

        FakePlayHead ph;

        // Main shot and the four themes: half a bar, the same audio and view each time
        {
            auto p = makeProcessor (a, ph, lengthHalfBar);
            auto e = makeEditor (*p, width, height);
            save (*e, e->getLocalBounds(), file ("site_main"));

            struct ThemeShot { int theme; const char* name; std::optional<juce::Colour> main, kick; };
            const ThemeShot shots[] = {
                { 0, "graphite", std::nullopt,                std::nullopt },                  // the defaults
                { 1, "midnight", juce::Colour (0xffffffff),   juce::Colour (0xff40d0ff) },     // white bass, cyan kick
                { 2, "phosphor", juce::Colour (0xff6cff8a),   juce::Colour (0xffffc23d) },     // green bass, amber kick
                { 3, "daylight", std::nullopt,                juce::Colour (0xff2563eb) },     // near-black bass, deep blue kick
            };
            for (auto& s : shots)
            {
                e->applyTheme (s.theme);
                setColours (*p, s.main, s.kick);
                e->getScopeView().refreshForTest();
                save (*e, e->getLocalBounds(), file (juce::String ("site_theme_") + s.name));
            }
            e->applyTheme (0);
            setColours (*p, std::nullopt, std::nullopt);

            // Zoomed in on the first ~14 ms of a kick (the kick lands at the start of the window)
            e->getScopeView().setViewForTest (0.0, 1.0 / 68.0);
            e->getScopeView().refreshForTest();
            save (*e, e->getLocalBounds(), file ("site_zoom"));
            e.reset();
            p->setPlayHead (nullptr);
        }

        // Settings, with no audio behind it: just the card, at 2x so the text is sharp
        {
            auto p = makeProcessor (a, ph, lengthHalfBar, false);
            p->getUISettings().removeProperty (UISettings::showCursorKey, nullptr);   // show the default
            auto e = makeEditor (*p, 900, 420);
            e->toggleSettings();
            if (auto* panel = e->getSettingsPanel())
            {
                const auto card = panel->getCardBounds().translated (panel->getX(), panel->getY());
                save (*e, card.expanded (1), file ("site_settings"));
            }
            e.reset();
            p->setPlayHead (nullptr);
        }

        // Filling a second monitor ("Make it as big as you like"): a 1920 x 920 window, rendered
        // at 1x so the header and controls keep their normal size and the scope fills the rest,
        // as on a real monitor. The kick goes into sidechain 2 this time (its default cyan),
        // and the window is one bar long.
        {
            auto p = makeProcessor (a, ph, lengthOneBar, true, 2);
            p->getUISettings().setProperty (UISettings::showHoverKey, false, nullptr);
            p->getUISettings().setProperty (UISettings::visibleKey (1), false, nullptr);   // hide the silent sidechain 1
            auto e = makeEditor (*p, 1920, 920);
            auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
            auto out = file ("site_fullscreen");
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat().writeImageToStream (img, os);
            e.reset();
            p->setPlayHead (nullptr);
        }

        // Frames for the demo loop, made to look like someone using it. The plugin shows the
        // normal arrow over the display and a grabbing hand while dragging, so the video does
        // too. At about Zoom x5 on a two-bar window, one beat is 62 % of the display: the arrow
        // rests on the next kick, grabs it, and pulls it to where the first one was (the view
        // moves with the hand, exactly as the plugin pans), lets go, then drags it back.
        // Forward and back keeps the loop seamless: the ruler labels are positions in the
        // window, so a one-way pan would jump at the seam.
        {
            auto p = makeProcessor (a, ph, lengthTwoBars);
            p->getUISettings().setProperty (UISettings::showHoverKey, false, nullptr);   // no readout box
            auto e = makeEditor (*p, width, height);
            auto& sv = e->getScopeView();
            const auto trace = sv.getTraceAreaForTest().translated ((float) sv.getX(), (float) sv.getY());

            const double beat = 1.0 / 8.0;                     // one beat of a two-bar window
            const double dragShare = 0.62;                     // one beat = 62 % of the display
            const double span = beat / dragShare;
            const double from = 2.0 * beat - 0.10 * span;      // kick on beat 3 sits 10 % from the left
            const float grabX = trace.getX() + 0.80f * trace.getWidth();   // on the next kick
            const float grabY = trace.getY() + 0.62f * trace.getHeight();
            const float dragPx = (float) dragShare * trace.getWidth();

            enum class Kind { rest, press, drag, release };
            struct Phase { Kind kind; int frames; int direction; };   // direction: +1 forward, -1 back
            const Phase phases[] = {
                { Kind::rest,    16,  0 },
                { Kind::press,    7,  0 },
                { Kind::drag,    50, +1 },
                { Kind::release,  7,  0 },
                { Kind::rest,    22,  0 },
                { Kind::press,    7,  0 },
                { Kind::drag,    50, -1 },
                { Kind::release,  7,  0 },
                { Kind::rest,     6,  0 },
            };

            constexpr int fps = 30;
            auto framesDir = outDir.getChildFile ("drag_frames");
            framesDir.deleteRecursively();
            framesDir.createDirectory();

            double offset = 0.0;   // 0 = first kick in place, 1 = dragged one beat forward
            int index = 0;
            for (auto& ph2 : phases)
            {
                for (int f = 0; f < ph2.frames; ++f)
                {
                    const bool down = ph2.kind == Kind::press || ph2.kind == Kind::drag
                                      || (ph2.kind == Kind::release && f < 2);
                    double o = offset;
                    float arc = 0.0f;
                    if (ph2.kind == Kind::drag)
                    {
                        const double t = (double) (f + 1) / (double) ph2.frames;
                        const double eased = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t);   // speed up, slow down
                        o = ph2.direction > 0 ? eased : 1.0 - eased;
                        arc = -5.0f * (float) std::sin (juce::MathConstants<double>::pi * t);              // a hand moves in a slight arc
                    }
                    // The hand holds the waveform: moving it left by dragPx moves the view one beat on
                    const float x = grabX - (float) o * dragPx;
                    const float y = grabY + arc;
                    sv.setViewForTest (from + o * beat, span);
                    sv.refreshForTest();

                    auto img = e->createComponentSnapshot (e->getLocalBounds(), true, scale);
                    {
                        juce::Graphics g (img);
                        g.addTransform (juce::AffineTransform::scale (scale));
                        drawPointer (g, { x, y }, down);
                    }
                    auto out = framesDir.getChildFile ("site_drag_" + juce::String (index++).paddedLeft ('0', 4) + ".png");
                    out.deleteFile();
                    juce::FileOutputStream os (out);
                    juce::PNGImageFormat().writeImageToStream (img, os);
                }
                if (ph2.kind == Kind::drag)
                    offset = ph2.direction > 0 ? 1.0 : 0.0;
            }
            std::cout << "  " << index << " video frames at " << fps << " fps" << std::endl;
            e.reset();
            p->setPlayHead (nullptr);
        }

        // Frames for the resize loop ("Make it as big as you like"). The plugin sits on the
        // product page's own background (#141517, so the video has no visible box) at its
        // normal size; the pointer drags the resize corner down to a strip
        // (watch the header and footer make room), then up to fill the frame, clicks the
        // corners button (scope only), brings the controls back from the top-left corner, and
        // drags back to the normal size, so the loop has no seam.
        {
            auto p = makeProcessor (a, ph, lengthHalfBar);
            p->getUISettings().setProperty (UISettings::showHoverKey, false, nullptr);   // no readout box
            const juce::Point<int> startSize { 760, 360 }, smallSize { 280, 140 };
            constexpr int canvasW = 1140, canvasH = 500, margin = 24;
            const juce::Point<int> bigSize { canvasW - 2 * margin, canvasH - 2 * margin };
            const juce::Point<float> origin { (float) margin, (float) margin };
            auto e = makeEditor (*p, startSize.x, startSize.y);

            auto framesDir = outDir.getChildFile ("resize_frames");
            framesDir.deleteRecursively();
            framesDir.createDirectory();
            int index = 0;

            juce::Point<float> pointer;   // in editor coordinates
            juce::Point<int> size = startSize;
            Pointer kind = Pointer::arrow;

            auto corner = [] (juce::Point<int> sz) { return juce::Point<float> ((float) sz.x - 9.0f, (float) sz.y - 9.0f); };
            auto ease = [] (double t) { return 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t); };
            auto lerp = [] (juce::Point<float> p0, juce::Point<float> p1, double t)
            {
                return p0 + (p1 - p0) * (float) t;
            };

            auto frame = [&]
            {
                if (e->getWidth() != size.x || e->getHeight() != size.y)
                {
                    e->setSize (size.x, size.y);
                    e->getScopeView().refreshForTest();
                }
                juce::Image canvas (juce::Image::ARGB, (int) (canvasW * scale), (int) (canvasH * scale), true);
                juce::Graphics g (canvas);
                g.addTransform (juce::AffineTransform::scale (scale));
                g.fillAll (juce::Colour (0xff141517));   // the product page's background
                auto shot = e->createComponentSnapshot (e->getLocalBounds(), true, scale);
                g.drawImage (shot, juce::Rectangle<float> (origin.x, origin.y, (float) size.x, (float) size.y));
                g.setColour (juce::Colour (0xff2c2f33));   // the page's 1 px outline around screenshots
                g.drawRect (juce::Rectangle<float> (origin.x - 1.0f, origin.y - 1.0f, (float) size.x + 2.0f, (float) size.y + 2.0f), 1.0f);
                drawPointer (g, origin + pointer, kind);
                auto out = framesDir.getChildFile ("site_resize_" + juce::String (index++).paddedLeft ('0', 4) + ".png");
                out.deleteFile();
                juce::FileOutputStream os (out);
                juce::PNGImageFormat().writeImageToStream (canvas, os);
            };

            auto hold = [&] (int frames) { for (int f = 0; f < frames; ++f) frame(); };

            auto moveTo = [&] (juce::Point<float> target, int frames, bool tellEditor = false)
            {
                const auto from = pointer;
                const float arcAmount = 0.08f * from.getDistanceFrom (target);
                for (int f = 1; f <= frames; ++f)
                {
                    const double t = ease ((double) f / frames);
                    pointer = lerp (from, target, t) + juce::Point<float> (0.0f, -arcAmount * (float) std::sin (juce::MathConstants<double>::pi * t));
                    if (tellEditor)   // so the scope-only view shows its way back, as with a real mouse
                    {
                        const juce::MouseEvent move (juce::Desktop::getInstance().getMainMouseSource(), pointer,
                                                     juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, e.get(), e.get(),
                                                     juce::Time::getCurrentTime(), pointer, juce::Time::getCurrentTime(), 0, false);
                        e->mouseMove (move);
                    }
                    frame();
                }
            };

            auto resizeTo = [&] (juce::Point<int> target, int frames)
            {
                const auto from = size;
                for (int f = 1; f <= frames; ++f)
                {
                    const double t = ease ((double) f / frames);
                    size = { juce::roundToInt (from.x + (target.x - from.x) * t), juce::roundToInt (from.y + (target.y - from.y) * t) };
                    pointer = corner (size);   // the corner stays under the pointer
                    frame();
                }
            };

            // Where the header's corners button is when the help button shows (see layoutHeader)
            auto scopeOnlyButton = [] (juce::Point<int> sz) { return juce::Point<float> ((float) sz.x - 76.0f, 18.0f); };
            const juce::Point<float> exitButton { 22.0f, 10.0f };
            const juce::Point<float> restSpot = corner (startSize) + juce::Point<float> (-70.0f, -60.0f);

            // About 8 seconds at 30 fps: short enough to stay a quiet detail on the page
            pointer = restSpot;
            hold (10);
            moveTo (corner (startSize), 12);
            kind = Pointer::resize;          hold (3);
            resizeTo (smallSize, 34);        hold (10);
            resizeTo (bigSize, 40);
            kind = Pointer::arrow;           hold (6);
            moveTo (scopeOnlyButton (bigSize), 14);
            hold (3);
            e->setScopeOnly (true);          hold (20);
            moveTo (exitButton, 16, true);
            hold (4);
            e->setScopeOnly (false);         hold (6);
            moveTo (corner (bigSize), 16);
            kind = Pointer::resize;          hold (3);
            resizeTo (startSize, 32);
            kind = Pointer::arrow;           hold (4);
            moveTo (restSpot, 10);
            hold (5);

            std::cout << "  " << index << " resize frames at 30 fps" << std::endl;
            e.reset();
            p->setPlayHead (nullptr);
        }

        std::cout << "Done" << std::endl;
        return 0;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc >= 3 && juce::String (argv[1]) == "--site")
        return site::render (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

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
        snapshot (*e2, "11_layers_custom_color");

        e2->toggleSettings();
        snapshot (*e2, "12_settings");
        e2->setSize (760, 360);
        snapshot (*e2, "13_settings_min");
        e2->toggleSettings();

        // Big screens: the old 3000 x 1600 cap must be gone, and a 4K window must draw
        if (auto* c = e2->getConstrainer())
        {
            check (c->getMaximumWidth() >= 7680 && c->getMaximumHeight() >= 4320,
                   "resize limit reaches 7680 x 4320");
            check (c->getMinimumWidth() <= 240 && c->getMinimumHeight() <= 110,
                   "resize limit goes down to 240 x 110");
        }
        e2->setSize (3840, 2160);
        check (e2->getWidth() == 3840 && e2->getHeight() == 2160, "editor can be 3840 x 2160");
        sv.refreshForTest();
        snapshot (*e2, "14_layers_4k");

        e2.reset();
        p2.setPlayHead (nullptr);
    }

    //--------------------------------------------------------------------------
    std::cout << "Colors, hex codes, small windows, scope only" << std::endl;
    {
        // Hex codes
        juce::Colour c;
        check (UISettings::parseHex ("#556BAA", c) && c == juce::Colour (0xff556baa), "hex #556BAA parses");
        check (UISettings::parseHex ("73848c", c) && c == juce::Colour (0xff73848c), "hex without # and in lower case parses");
        check (UISettings::parseHex ("#5ab", c) && c == juce::Colour (0xff55aabb), "short hex #5AB parses as #55AABB");
        check (! UISettings::parseHex ("#12345", c) && ! UISettings::parseHex ("zzzzzz", c) && ! UISettings::parseHex ("", c),
               "invalid hex is rejected");
        check (UISettings::toHex (juce::Colour (0xff0a0b0c)) == "#0A0B0C", "colors are shown as #RRGGBB");

        // Random colours: readable on every theme, spread out, clear of Main and Hold
        juce::Random rng (1234);
        bool contrastOk = true, spreadOk = true, avoidOk = true;
        float worstContrast = 99.0f, worstSpread = 1.0f;
        for (int ti = 0; ti < numThemes; ++ti)
        {
            const auto& th = themes[ti];
            const auto mainC = UISettings::defaultLayerColour (0, th);
            for (int run = 0; run < 300; ++run)
            {
                const auto cs = UISettings::randomSidechainColours (th, rng, mainC, th.hold);
                for (int i = 0; i < 3; ++i)
                {
                    const float cr = UISettings::contrastRatio (cs[(size_t) i], th.scopeBg);
                    worstContrast = juce::jmin (worstContrast, cr);
                    contrastOk = contrastOk && cr >= UISettings::minRandomContrast;
                    for (auto avoidC : { mainC, th.hold })
                        if (avoidC.getSaturation() > 0.25f)
                            avoidOk = avoidOk && UISettings::hueDistance (cs[(size_t) i].getHue(), avoidC.getHue())
                                                     >= UISettings::minAvoidHueGap - 0.005f;
                    for (int k = i + 1; k < 3; ++k)
                    {
                        const float d = UISettings::hueDistance (cs[(size_t) i].getHue(), cs[(size_t) k].getHue());
                        worstSpread = juce::jmin (worstSpread, d);
                        spreadOk = spreadOk && d >= UISettings::minRandomHueGap - 0.01f;
                    }
                }
            }
        }
        check (contrastOk, "random colors are readable on every theme (worst contrast " + juce::String (worstContrast, 2) + ")");
        check (spreadOk, "random colors are spread around the wheel (closest pair " + juce::String (worstSpread * 360.0f, 0) + " deg)");
        check (avoidOk, "random colors keep clear of the Main and Hold colors");

        // A processor with all three sidechains
        SmartscopeAudioProcessor p3;
        auto layout = p3.getBusesLayout();
        for (int b = 1; b <= 3; ++b)
            layout.inputBuses.getReference (b) = juce::AudioChannelSet::stereo();
        check (p3.setBusesLayout (layout), "three stereo sidechains accepted");
        FakePlayHead ph3;
        p3.setPlayHead (&ph3);
        p3.setRateAndBufferSizeDetails (sr, 512);
        p3.prepareToPlay (sr, 512);
        if (auto* lp = p3.apvts.getParameter (ParamIDs::length))
            lp->setValueNotifyingHost (lp->convertTo0to1 (3.0f));   // 1/2 bar
        renderLayers (p3, ph3, sr, 2.5, 60.0);

        bool allOpaque = true;
        for (int layer = 0; layer < numLayers; ++layer)
            allOpaque = allOpaque && UISettings::layerOpacity (p3.getUISettings(), layer) == 1.0f;
        check (allOpaque, "every layer starts at 100% opacity");

        std::unique_ptr<SmartscopeAudioProcessorEditor> e4 (
            dynamic_cast<SmartscopeAudioProcessorEditor*> (p3.createEditor()));
        e4->setSize (900, 480);
        auto& sv = e4->getScopeView();
        sv.refreshForTest();
        snapshot (*e4, "15_default_opaque");

        e4->toggleSettings();
        check (e4->isSettingsOpen(), "Settings opens at 900 x 480");
        e4->getSettingsPanel()->randomiseSidechainColours();
        snapshot (*e4, "16_settings_hex_random");
        e4->toggleSettings();
        sv.refreshForTest();
        snapshot (*e4, "17_random_colors");

        e4->toggleSettings();
        e4->setSize (700, 420);
        check (! e4->isSettingsOpen(), "Settings closes when the window gets narrower than it");
        e4->setSize (900, 480);
        e4->toggleSettings();
        e4->setSize (900, 340);
        check (! e4->isSettingsOpen(), "Settings closes when the window gets shorter than it");
        e4->toggleSettings();
        check (! e4->isSettingsOpen(), "Settings doesn't open in a window too small for it");
        e4->setSize (760, 352);
        e4->toggleSettings();
        check (e4->isSettingsOpen(), "Settings opens at its minimum, 760 x 352");
        snapshot (*e4, "18_settings_at_minimum");
        e4->toggleSettings();

        // Small windows: every visible control inside the window, none overlapping
        for (auto size : { juce::Point<int> (640, 300), juce::Point<int> (480, 220), juce::Point<int> (340, 160),
                           juce::Point<int> (240, 110) })
        {
            e4->setSize (size.x, size.y);
            sv.refreshForTest();
            const juce::String tag = juce::String (size.x) + "x" + juce::String (size.y);
            bool inside = true, apart = true;
            juce::Array<juce::Component*> shown;
            for (auto* ch : e4->getChildren())
                if (ch->isVisible() && ch != &sv)
                    shown.add (ch);   // includes JUCE's resize corner, so nothing may sit under it
            for (auto* a : shown)
            {
                inside = inside && e4->getLocalBounds().contains (a->getBounds());
                for (auto* b : shown)
                    if (a != b && a->getBounds().intersects (b->getBounds()))
                        apart = false;
            }
            check (inside && apart, tag + ": controls fit and don't overlap (" + juce::String (shown.size()) + " shown)");
            if (! (inside && apart))
                for (auto* a : shown)
                    std::cout << "        '" << a->getName() << "' " << a->getBounds().toString() << std::endl;
            check (sv.getHeight() >= 60, tag + ": the display keeps at least 60 px of height (" + juce::String (sv.getHeight()) + ")");
            snapshot (*e4, "19_small_" + tag);
        }

        // Scope only
        e4->setSize (900, 480);
        e4->setScopeOnly (true);
        sv.refreshForTest();
        auto countOthers = [&]
        {
            int n = 0;
            for (auto* ch : e4->getChildren())
                if (ch->isVisible() && ch != &sv && dynamic_cast<juce::ResizableCornerComponent*> (ch) == nullptr)
                    ++n;
            return n;
        };
        int others = countOthers();
        check (sv.getBounds() == e4->getLocalBounds() && others == 0,
               "scope only: the display fills the window, nothing else shows (apart from the resize corner)");
        snapshot (*e4, "20_scope_only");

        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const juce::MouseEvent move (src, { 300.0f, 200.0f }, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     e4.get(), e4.get(), juce::Time::getCurrentTime(), { 300.0f, 200.0f },
                                     juce::Time::getCurrentTime(), 0, false);
        e4->mouseMove (move);
        others = countOthers();
        check (others == 1, "scope only: moving the mouse shows the way back");
        snapshot (*e4, "21_scope_only_mouse");

        juce::MemoryBlock state;
        p3.getStateInformation (state);
        SmartscopeAudioProcessor p4;
        p4.setStateInformation (state.getData(), (int) state.getSize());
        check (UISettings::flag (p4.getUISettings(), UISettings::scopeOnlyKey, false), "scope only is saved with the project");

        e4->setScopeOnly (false);
        check (sv.getBounds() != e4->getLocalBounds() && e4->getSettingsPanel() == nullptr,
               "leaving scope only brings the header and footer back");
        e4.reset();
        p3.setPlayHead (nullptr);
    }

    //--------------------------------------------------------------------------
    std::cout << "Window size when the plugin opens" << std::endl;
    {
        SmartscopeAudioProcessor p5;
        std::unique_ptr<juce::AudioProcessorEditor> e5 (p5.createEditor());
        check (e5->getWidth() == 760 && e5->getHeight() == 360,
               "a new instance opens at 760 x 360 (got " + juce::String (e5->getWidth()) + " x " + juce::String (e5->getHeight()) + ")");

        e5->setSize (1234, 567);
        e5.reset();
        e5.reset (p5.createEditor());
        check (e5->getWidth() == 1234 && e5->getHeight() == 567,
               "reopening the window keeps its size (got " + juce::String (e5->getWidth()) + " x " + juce::String (e5->getHeight()) + ")");
        e5.reset();

        juce::MemoryBlock state;
        p5.getStateInformation (state);
        SmartscopeAudioProcessor p6;
        p6.setStateInformation (state.getData(), (int) state.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> e6 (p6.createEditor());
        check (e6->getWidth() == 1234 && e6->getHeight() == 567,
               "a saved project reopens at its saved size (got " + juce::String (e6->getWidth()) + " x " + juce::String (e6->getHeight()) + ")");
        e6.reset();

        SmartscopeAudioProcessor p7;
        p7.editorWidth = 100000; p7.editorHeight = 1;   // nonsense in a saved project
        std::unique_ptr<juce::AudioProcessorEditor> e7 (p7.createEditor());
        check (e7->getWidth() == 7680 && e7->getHeight() == 110, "an out-of-range saved size is clamped to the limits");
    }

    std::cout << (failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
