#include "ScopeView.h"

#include <cmath>
#include <iterator>
#include <numeric>

namespace
{
    constexpr float rulerHeight = 20.0f;
    constexpr float leftMargin  = 44.0f;   // room for the amplitude labels
    constexpr float ampScale    = 0.95f;   // 0 dBFS sits just inside the top/bottom edge

    juce::String formatWholeNoteFraction (double quarterNotes)
    {
        // Express a position as a fraction of a whole note, like "3/16" or "1 1/4"
        constexpr int den = 128;
        int num = (int) std::lround (quarterNotes / 4.0 * den);
        if (num == 0)
            return "0";

        const int whole = num / den;
        const int rem   = num % den;

        juce::String s;
        if (whole > 0)
            s << whole;

        if (rem != 0)
        {
            const int g = std::gcd (rem, den);
            if (s.isNotEmpty())
                s << " ";
            s << (rem / g) << "/" << (den / g);
        }
        return s;
    }

    juce::String formatMs (double ms, double step)
    {
        const int decimals = step >= 1.0 ? 0 : (step >= 0.1 ? 1 : 2);
        if (std::abs (ms) < step * 0.01)
            ms = 0.0;
        return juce::String (ms, decimals) + " ms";
    }
}

//==============================================================================
ScopeView::ScopeView (SmartscopeAudioProcessor& p) : proc (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    startTimerHz (60);
}

ScopeView::~ScopeView()
{
    stopTimer();
}

juce::Rectangle<float> ScopeView::getRulerArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (leftMargin).removeFromTop (rulerHeight);
}

juce::Rectangle<float> ScopeView::getTraceArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (leftMargin).withTrimmedTop (rulerHeight).reduced (0.0f, 6.0f);
}

//==============================================================================
float ScopeView::getGainLinear() const
{
    const float db = proc.apvts.getRawParameterValue (ParamIDs::gain)->load();
    return juce::Decibels::decibelsToGain (db);
}

void ScopeView::nudgeGainDb (float deltaDb)
{
    if (auto* param = proc.apvts.getParameter (ParamIDs::gain))
    {
        const float current = proc.apvts.getRawParameterValue (ParamIDs::gain)->load();
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (current + deltaDb));
        param->endChangeGesture();
    }
}

void ScopeView::stepLength (int delta)
{
    if (auto* param = proc.apvts.getParameter (ParamIDs::length))
    {
        const int current = (int) std::lround (proc.apvts.getRawParameterValue (ParamIDs::length)->load());
        const int next = juce::jlimit (0, numLengthChoices - 1, current + delta);
        if (next != current)
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
            param->endChangeGesture();
        }
    }
}

//==============================================================================
void ScopeView::timerCallback()
{
    const bool  lpOn = proc.apvts.getRawParameterValue (ParamIDs::lowpassOn)->load() > 0.5f;
    const float lpHz = proc.apvts.getRawParameterValue (ParamIDs::lowpassHz)->load();

    const auto wc = proc.getWriteCounter();
    const auto rc = proc.getResetCounter();

    bool connectionChanged = false;
    for (int layer = 1; layer < numLayers; ++layer)
        connectionChanged = connectionChanged || proc.isLayerConnected (layer) != lastConnected[layer];

    if (connectionChanged || wc != lastWriteCounter || rc != lastResetCounter || lpOn != lastLpOn
        || (lpOn && std::abs (lpHz - lastLpHz) > 0.01f) || needsRebuild)
    {
        rebuild();
        repaint();
    }
    else if (hovering || proc.isHostPlaying())
    {
        repaint();
    }
}

void ScopeView::rebuild()
{
    const bool  lpOn = proc.apvts.getRawParameterValue (ParamIDs::lowpassOn)->load() > 0.5f;
    const float lpHz = proc.apvts.getRawParameterValue (ParamIDs::lowpassHz)->load();

    const bool filterChanged = lpOn != lastLpOn || (lpOn && std::abs (lpHz - lastLpHz) > 0.01f);

    lastWriteCounter = proc.getWriteCounter();
    lastResetCounter = proc.getResetCounter();
    lastLpOn = lpOn;
    lastLpHz = lpHz;
    needsRebuild = false;

    sampleRate = proc.getDisplaySampleRate();
    windowQN   = proc.getWindowQuarterNotes();
    barQN      = proc.getBarQuarterNotes();

    const int n = proc.getWindowSamples();
    const int wp = juce::jlimit (0, juce::jmax (0, n - 1), proc.getWritePosition());

    const bool sizeChanged = n != windowN;
    windowN  = n;
    writePos = wp;

    if (sizeChanged)
    {
        clampView();
        notifyViewChanged();
    }

    const float* src = proc.getDisplayData();

    float peak = 0.0f;
    clipRuns.clear();
    int runStart = -1;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (src[i]);
        peak = juce::jmax (peak, a);

        if (a > 1.0f)
        {
            if (runStart < 0)
                runStart = i;
        }
        else if (runStart >= 0)
        {
            clipRuns.emplace_back (runStart, i - 1);
            runStart = -1;
        }
    }
    if (runStart >= 0)
        clipRuns.emplace_back (runStart, n - 1);
    windowPeak = peak;

    processInto (src, n, wp, layerData[0], lpOn, lpHz);

    for (int layer = 1; layer < numLayers; ++layer)
    {
        layerShown[layer] = proc.isLayerConnected (layer);
        lastConnected[layer] = layerShown[layer];
        if (layerShown[layer])
            processInto (proc.getDisplayData (layer), n, wp, layerData[layer], lpOn, lpHz);
        else
            layerData[layer].clear();
    }

    if (holdActive && filterChanged)
        processInto (holdRaw.data(), (int) holdRaw.size(), holdWritePos, holdData, lpOn, lpHz);
}

void ScopeView::processInto (const float* src, int n, int wp, std::vector<float>& dest, bool lpOn, float lpHz)
{
    dest.resize ((size_t) juce::jmax (0, n));
    if (n <= 0)
        return;

    if (! lpOn || n < 8)
    {
        std::copy (src, src + n, dest.begin());
        return;
    }

    // The buffer is circular: everything from the write position onwards is the older
    // part, and 0..writePos-1 is the newest. Unwrap it into time order so the filter
    // runs over continuous audio, then put it back in window order.
    filterScratch.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        filterScratch[(size_t) i] = src[(wp + i) % n];

    zeroPhaseLowpass (filterScratch.data(), n, (double) lpHz);

    for (int i = 0; i < n; ++i)
        dest[(size_t) ((wp + i) % n)] = (float) filterScratch[(size_t) i];
}

void ScopeView::zeroPhaseLowpass (double* x, int n, double cutoff)
{
    // A 2nd-order Butterworth low-pass run forwards and then backwards: no phase shift
    // and no delay, so the filtered waveform stays exactly on the grid (important when
    // the point is to judge timing and phase).
    const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    cutoff = juce::jlimit (5.0, fs * 0.45, cutoff);

    const double w0 = juce::MathConstants<double>::twoPi * cutoff / fs;
    const double cw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * 0.70710678118654752);
    const double a0 = 1.0 + alpha;
    const double b0 = (1.0 - cw) * 0.5 / a0;
    const double b1 = (1.0 - cw) / a0;
    const double b2 = b0;
    const double a1 = -2.0 * cw / a0;
    const double a2 = (1.0 - alpha) / a0;

    // Odd-reflection padding at both ends to keep the edges free of start-up artefacts
    const int pad = juce::jmin (n - 1, (int) (3.0 * fs / cutoff));
    std::vector<double> ext ((size_t) (n + 2 * pad));
    for (int i = 0; i < n; ++i)
        ext[(size_t) (pad + i)] = x[i];
    for (int k = 0; k < pad; ++k)
    {
        ext[(size_t) (pad - 1 - k)]   = 2.0 * x[0] - x[k + 1];
        ext[(size_t) (pad + n + k)]   = 2.0 * x[n - 1] - x[n - 2 - k];
    }

    auto runBiquad = [&] (auto begin, auto end)
    {
        const double c = *begin;          // start in the steady state for the first value
        double z1 = c * (1.0 - b0);
        double z2 = c * (b2 - a2);
        for (auto it = begin; it != end; ++it)
        {
            const double in = *it;
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            *it = out;
        }
    };

    runBiquad (ext.begin(), ext.end());
    runBiquad (ext.rbegin(), ext.rend());

    for (int i = 0; i < n; ++i)
        x[i] = ext[(size_t) (pad + i)];
}

//==============================================================================
void ScopeView::setHold (bool shouldHold)
{
    holdActive = shouldHold;

    if (holdActive)
    {
        const int n = proc.getWindowSamples();
        const float* src = proc.getDisplayData();
        holdRaw.assign (src, src + n);
        holdWritePos = juce::jlimit (0, juce::jmax (0, n - 1), proc.getWritePosition());

        const bool  lpOn = proc.apvts.getRawParameterValue (ParamIDs::lowpassOn)->load() > 0.5f;
        const float lpHz = proc.apvts.getRawParameterValue (ParamIDs::lowpassHz)->load();
        processInto (holdRaw.data(), n, holdWritePos, holdData, lpOn, lpHz);
    }
    else
    {
        holdRaw.clear();
        holdData.clear();
    }

    repaint();
    notifyViewChanged();
}

//==============================================================================
void ScopeView::clampView()
{
    const double minSpan = windowN > 0 ? juce::jmin (1.0, 8.0 / (double) windowN) : 1.0e-6;
    viewSpan  = juce::jlimit (minSpan, 1.0, viewSpan);
    viewStart = juce::jlimit (0.0, 1.0 - viewSpan, viewStart);
}

void ScopeView::zoomAround (double xFraction, double factor)
{
    const double anchor = viewStart + xFraction * viewSpan;
    viewSpan *= factor;
    clampView();
    viewStart = anchor - xFraction * viewSpan;
    clampView();
    repaint();
    notifyViewChanged();
}

void ScopeView::resetZoom()
{
    viewStart = 0.0;
    viewSpan = 1.0;
    repaint();
    notifyViewChanged();
}

void ScopeView::notifyViewChanged()
{
    if (onViewChanged)
        onViewChanged();
}

//==============================================================================
void ScopeView::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.position;
    hovering = getTraceArea().contains (e.position) || getRulerArea().contains (e.position);
    repaint();
}

void ScopeView::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

void ScopeView::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragStartView = viewStart;
    hoverPos = e.position;
    if (viewSpan < 1.0)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ScopeView::mouseDrag (const juce::MouseEvent& e)
{
    const auto area = getTraceArea();
    if (area.getWidth() <= 0.0f)
        return;

    hoverPos = e.position;
    viewStart = dragStartView - (double) e.getDistanceFromDragStartX() / (double) area.getWidth() * viewSpan;
    clampView();
    repaint();
}

void ScopeView::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ScopeView::mouseDoubleClick (const juce::MouseEvent&)
{
    resetZoom();
}

void ScopeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto area = getTraceArea();
    const double xFrac = area.getWidth() > 0.0f
                           ? juce::jlimit (0.0, 1.0, (double) ((e.position.x - area.getX()) / area.getWidth()))
                           : 0.5;

    const float dy = w.isReversed ? -w.deltaY : w.deltaY;
    const float dx = w.isReversed ? -w.deltaX : w.deltaX;

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Ctrl + wheel: vertical gain
        nudgeGainDb (dy * 10.0f);
    }
    else if (e.mods.isAltDown())
    {
        // Alt + wheel: change the window length (accumulate for smooth trackpads)
        wheelAccumulator += (std::abs (dy) > std::abs (dx) ? dy : dx);
        if (std::abs (wheelAccumulator) >= 0.12f)
        {
            stepLength (wheelAccumulator > 0.0f ? 1 : -1);
            wheelAccumulator = 0.0f;
        }
    }
    else if (e.mods.isShiftDown() || std::abs (dx) > std::abs (dy))
    {
        // Shift + wheel (or horizontal trackpad swipe): scroll
        const float d = std::abs (dy) > std::abs (dx) ? dy : dx;
        viewStart -= (double) d * viewSpan * 0.5;
        clampView();
        repaint();
    }
    else
    {
        // Plain wheel: zoom in/out around the mouse
        zoomAround (xFrac, std::exp (-(double) dy * 2.0));
    }
}

void ScopeView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    const auto area = getTraceArea();
    const double xFrac = area.getWidth() > 0.0f
                           ? juce::jlimit (0.0, 1.0, (double) ((e.position.x - area.getX()) / area.getWidth()))
                           : 0.5;
    if (scaleFactor > 0.0f)
        zoomAround (xFrac, 1.0 / (double) scaleFactor);
}


//==============================================================================
void ScopeView::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.scopeBg);

    const auto ruler = getRulerArea();
    const auto trace = getTraceArea();
    const float gain = getGainLinear();

    g.setColour (t.rulerBg);
    g.fillRect (getLocalBounds().toFloat().removeFromTop (rulerHeight));

    drawGrid (g, trace, ruler);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (trace.expanded (0.0f, 4.0f).toNearestInt());

        auto ui = proc.getUISettings();

        if (holdActive && ! holdData.empty())
            drawTrace (g, holdData, trace, UISettings::holdColour (ui, t).withMultipliedAlpha (0.85f), gain, 1.6f, 0.0f);

        auto drawLayer = [&] (int layer)
        {
            if (! layerShown[layer] || layerData[layer].empty() || ! UISettings::layerVisible (ui, layer))
                return;
            const auto colour = UISettings::layerColour (ui, layer, t).withMultipliedAlpha (UISettings::layerOpacity (ui, layer));
            drawTrace (g, layerData[layer], trace, colour, gain, 1.6f, layer == 0 ? t.glow : 0.0f);
        };

        // By default the sidechain layers sit on top of the main input
        if (UISettings::flag (ui, UISettings::mainOnTopKey, false))
        {
            for (int layer = numLayers - 1; layer >= 1; --layer) drawLayer (layer);
            drawLayer (0);
        }
        else
        {
            drawLayer (0);
            for (int layer = 1; layer < numLayers; ++layer) drawLayer (layer);
        }

        // Play cursor
        if (proc.isHostPlaying() && windowN > 0 && UISettings::flag (ui, UISettings::showCursorKey, true))
        {
            const double frac = ((double) writePos / (double) windowN - viewStart) / viewSpan;
            if (frac >= 0.0 && frac <= 1.0)
            {
                const float x = trace.getX() + (float) frac * trace.getWidth();
                g.setColour (t.accent.withAlpha (0.6f));
                g.fillRect (x - 0.5f, trace.getY(), 1.0f, trace.getHeight());
            }
        }
    }

    auto ui = proc.getUISettings();
    if (UISettings::flag (ui, UISettings::showClipKey, true))
        drawClipMarkers (g, trace);

    // Peak readout (top right of the trace area)
    {
        const bool clipping = windowPeak > 1.0f;
        const float peakDb = juce::Decibels::gainToDecibels (windowPeak, -100.0f);
        const juce::String peakText = "Peak " + (windowPeak > 0.0f ? juce::String (peakDb, 1) + " dB" : juce::String ("-inf"));
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (clipping ? t.clip : t.textDim);
        g.drawText (peakText, trace.withHeight (16.0f).withTrimmedRight (8.0f).translated (0.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    if (hovering && ! dragging && UISettings::flag (ui, UISettings::showHoverKey, true))
        drawHover (g, trace, gain);

    g.setColour (t.outline);
    g.drawRect (getLocalBounds().toFloat(), 1.0f);
}

void ScopeView::drawClipMarkers (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (clipRuns.empty() || windowN <= 0)
        return;

    const auto& t = theme();
    const double startS = viewStart * windowN;
    const double visS = viewSpan * windowN;
    const double pxPerSample = (double) area.getWidth() / visS;

    g.setColour (t.clip);
    for (auto [a, b] : clipRuns)
    {
        if ((double) b + 1.0 < startS || (double) a > startS + visS)
            continue;

        float x0 = area.getX() + (float) (((double) a - startS) * pxPerSample);
        float x1 = area.getX() + (float) (((double) b + 1.0 - startS) * pxPerSample);
        x0 = juce::jmax (x0, area.getX());
        x1 = juce::jmin (x1, area.getRight());
        const float w = juce::jmax (2.0f, x1 - x0);

        // A short red tick at the top edge of the display
        g.fillRect (x0, area.getY() - 6.0f, w, 12.0f);
    }

}

void ScopeView::drawGrid (juce::Graphics& g, juce::Rectangle<float> trace, juce::Rectangle<float> ruler)
{
    const auto& t = theme();
    const float W = trace.getWidth();
    if (W <= 0.0f || windowQN <= 0.0)
        return;

    g.setFont (juce::FontOptions (11.0f));

    auto hLine = [&] (float y, juce::Colour c)
    {
        g.setColour (c);
        g.fillRect (trace.getX(), std::round (y) - 0.5f, W, 1.0f);
    };
    auto vLine = [&] (float x, juce::Colour c)
    {
        g.setColour (c);
        g.fillRect (std::round (x) - 0.5f, trace.getY(), 1.0f, trace.getHeight());
    };
    auto tick = [&] (float x, float len, juce::Colour c)
    {
        g.setColour (c);
        g.fillRect (std::round (x) - 0.5f, ruler.getBottom() - len, 1.0f, len);
    };

    //--------------------------------------------------------------------------
    // Amplitude grid: linear scale, labelled in dB. 0 dB = full scale.
    const float gain = getGainLinear();
    const float cy = trace.getCentreY();
    const float halfH = trace.getHeight() * 0.5f;

    auto ampLabel = [&] (float y, const juce::String& text)
    {
        g.setColour (t.textDim);
        g.drawText (text, juce::Rectangle<float> (0.0f, y - 7.0f, leftMargin - 8.0f, 14.0f), juce::Justification::centredRight);
    };

    float lastOffset = 0.0f;
    for (int db = 0; db >= -60; db -= 6)
    {
        const float a = juce::Decibels::decibelsToGain ((float) db);
        const float offset = a * gain * halfH * ampScale;
        if (offset > halfH + 1.0f)
            continue;
        if (offset < 14.0f || (lastOffset > 0.0f && lastOffset - offset < 18.0f))
            continue;
        lastOffset = offset;

        for (float sign : { -1.0f, 1.0f })
        {
            const float y = cy + sign * offset;
            hLine (y, db == 0 ? t.gridBar : t.gridSub);
            ampLabel (y, juce::String (db));
        }
    }
    hLine (cy, t.centreLine);
    ampLabel (cy, "-inf");

    //--------------------------------------------------------------------------
    // Time grid: musical subdivisions, or milliseconds when zoomed in very far
    const double visQN = viewSpan * windowQN;
    const double qnStart = viewStart * windowQN;
    const double pxPerQN = (double) W / visQN;

    auto xForQN = [&] (double qn) { return trace.getX() + (float) ((qn - qnStart) * pxPerQN); };

    auto drawLabel = [&] (float x, const juce::String& text, juce::Colour c)
    {
        if (x < ruler.getRight() - 24.0f)
        {
            g.setColour (c);
            g.drawText (text, juce::Rectangle<float> (x + 4.0f, ruler.getY(), 80.0f, ruler.getHeight() - 3.0f),
                        juce::Justification::centredLeft);
        }
    };

    auto pickMinor = [] (double step, double pxPerUnit)
    {
        if (step / 4.0 * pxPerUnit >= 14.0) return step / 4.0;
        if (step / 2.0 * pxPerUnit >= 14.0) return step / 2.0;
        return 0.0;
    };

    constexpr double finestMusical = 4.0 / 128.0;   // a 1/128 note, in quarter notes
    const bool msMode = finestMusical * pxPerQN > 140.0 && windowN > 0 && sampleRate > 0.0;

    if (! msMode)
    {
        static const double steps[] = { 4.0 / 128.0, 4.0 / 64.0, 4.0 / 32.0, 4.0 / 16.0, 4.0 / 8.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
        double step = steps[std::size (steps) - 1];
        for (double s : steps)
            if (s * pxPerQN >= 64.0) { step = s; break; }

        if (const double minor = pickMinor (step, pxPerQN); minor > 0.0)
        {
            const double first = std::ceil (qnStart / minor - 1.0e-9) * minor;
            for (double qn = first; qn <= qnStart + visQN + 1.0e-9; qn += minor)
            {
                if (std::abs (std::remainder (qn, step)) < 1.0e-9)
                    continue;
                vLine (xForQN (qn), t.gridMinor);
                tick (xForQN (qn), 3.0f, t.gridSub);
            }
        }

        const double first = std::ceil (qnStart / step - 1.0e-9) * step;
        for (double qn = first; qn <= qnStart + visQN + 1.0e-9; qn += step)
        {
            const float x = xForQN (qn);
            const bool isBar  = std::abs (std::remainder (qn, barQN)) < 1.0e-6;
            const bool isBeat = std::abs (std::remainder (qn, 1.0)) < 1.0e-6;

            vLine (x, isBar ? t.gridBar : (isBeat ? t.gridBeat : t.gridSub));
            tick (x, isBeat ? 7.0f : 5.0f, isBar ? t.textDim : t.gridBar);
            drawLabel (x, formatWholeNoteFraction (qn), isBeat ? t.text : t.textDim);
        }
    }
    else
    {
        const double windowMs = (double) windowN / sampleRate * 1000.0;
        const double msStart = viewStart * windowMs;
        const double visMs = viewSpan * windowMs;
        const double pxPerMs = (double) W / visMs;

        static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0 };
        double step = 100.0;
        for (double s : steps)
            if (s * pxPerMs >= 64.0) { step = s; break; }

        if (const double minor = pickMinor (step, pxPerMs); minor > 0.0)
        {
            const double first = std::ceil (msStart / minor - 1.0e-9) * minor;
            for (double ms = first; ms <= msStart + visMs + 1.0e-9; ms += minor)
            {
                if (std::abs (std::remainder (ms, step)) < step * 1.0e-6)
                    continue;
                const float x = trace.getX() + (float) ((ms - msStart) * pxPerMs);
                vLine (x, t.gridMinor);
                tick (x, 3.0f, t.gridSub);
            }
        }

        const double first = std::ceil (msStart / step - 1.0e-9) * step;
        for (double ms = first; ms <= msStart + visMs + 1.0e-9; ms += step)
        {
            const float x = trace.getX() + (float) ((ms - msStart) * pxPerMs);
            vLine (x, t.gridSub);
            tick (x, 5.0f, t.gridBar);
            drawLabel (x, formatMs (ms, step), t.textDim);
        }

        // Musical positions still matter when zoomed in: mark any that are visible
        const double firstQ = std::ceil (qnStart / finestMusical - 1.0e-9) * finestMusical;
        for (double qn = firstQ; qn <= qnStart + visQN + 1.0e-9; qn += finestMusical)
            vLine (xForQN (qn), t.accent.withAlpha (0.35f));
    }
}

void ScopeView::drawTrace (juce::Graphics& g, const std::vector<float>& d, juce::Rectangle<float> area,
                           juce::Colour colour, float gain, float lineWidth, float glow)
{
    const int n = (int) d.size();
    if (n < 2 || area.getWidth() <= 0.0f)
        return;

    const float cy = area.getCentreY();
    const float scale = area.getHeight() * 0.5f * ampScale * gain;
    const float top = area.getY() - 3.0f, bottom = area.getBottom() + 3.0f;

    auto yFor = [&] (float v) { return juce::jlimit (top, bottom, cy - v * scale); };

    const double startS = viewStart * n;
    const double visS   = viewSpan * n;

    const float pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int cols = juce::jmax (1, (int) std::ceil (area.getWidth() * pixelScale));
    const double spc = visS / (double) cols;

    juce::Path p;
    bool isEnvelope = false;

    if (spc > 1.0)
    {
        // Dense: build the min/max envelope (one column per physical pixel) as a single
        // closed shape. Filled and given a thin outline, it reads as solid where the
        // signal is busy, keeps a minimum thickness where it is sparse, and has smooth
        // anti-aliased edges throughout.
        isEnvelope = true;
        const float colW = area.getWidth() / (float) cols;
        envHi.resize ((size_t) cols);
        envLo.resize ((size_t) cols);

        for (int c = 0; c < cols; ++c)
        {
            const int s0 = juce::jlimit (0, n - 1, (int) std::floor (startS + c * spc));
            const int s1 = juce::jlimit (0, n - 1, (int) std::floor (startS + (c + 1) * spc));

            float lo = d[(size_t) s0], hi = lo;
            for (int s = s0 + 1; s <= s1; ++s)
            {
                const float v = d[(size_t) s];
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
            envHi[(size_t) c] = yFor (hi);
            envLo[(size_t) c] = yFor (lo);
        }

        p.preallocateSpace (cols * 6 + 8);
        auto xFor = [&] (int c) { return area.getX() + ((float) c + 0.5f) * colW; };

        p.startNewSubPath (xFor (0), envHi[0]);
        for (int c = 1; c < cols; ++c)
            p.lineTo (xFor (c), envHi[(size_t) c]);
        for (int c = cols - 1; c >= 0; --c)
            p.lineTo (xFor (c), envLo[(size_t) c]);
        p.closeSubPath();
    }
    else
    {
        // Zoomed in: connect the individual samples
        const int i0 = juce::jlimit (0, n - 1, (int) std::floor (startS) - 1);
        const int i1 = juce::jlimit (0, n - 1, (int) std::ceil (startS + visS) + 1);
        const double pxPerSample = (double) area.getWidth() / visS;
        auto xFor = [&] (int i) { return area.getX() + (float) (((double) i - startS) * pxPerSample); };

        p.preallocateSpace ((i1 - i0 + 1) * 3);
        p.startNewSubPath (xFor (i0), yFor (d[(size_t) i0]));
        for (int i = i0 + 1; i <= i1; ++i)
            p.lineTo (xFor (i), yFor (d[(size_t) i]));
    }

    const juce::PathStrokeType stroke (isEnvelope ? lineWidth * 0.75f : lineWidth,
                                       juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (glow > 0.0f)
    {
        g.setColour (colour.withMultipliedAlpha (glow * 0.5f));
        g.strokePath (p, juce::PathStrokeType (lineWidth * 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withMultipliedAlpha (glow));
        g.strokePath (p, juce::PathStrokeType (lineWidth * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (colour);
    if (isEnvelope)
        g.fillPath (p);
    g.strokePath (p, stroke);

    // Sample dots once samples are far apart
    if (! isEnvelope)
    {
        const double pxPerSample = (double) area.getWidth() / visS;
        if (pxPerSample > 7.0)
        {
            const int i0 = juce::jlimit (0, n - 1, (int) std::floor (startS) - 1);
            const int i1 = juce::jlimit (0, n - 1, (int) std::ceil (startS + visS) + 1);
            const float r = pxPerSample > 16.0 ? 2.6f : 1.9f;
            for (int i = i0; i <= i1; ++i)
            {
                const float x = area.getX() + (float) (((double) i - startS) * pxPerSample);
                g.fillEllipse (x - r, yFor (d[(size_t) i]) - r, r * 2.0f, r * 2.0f);
            }
        }
    }
}

void ScopeView::drawHover (juce::Graphics& g, juce::Rectangle<float> area, float gain)
{
    if (windowN <= 0 || data.empty() || area.getWidth() <= 0.0f)
        return;

    const auto& t = theme();
    const float x = juce::jlimit (area.getX(), area.getRight(), hoverPos.x);
    const double frac = viewStart + (double) ((x - area.getX()) / area.getWidth()) * viewSpan;
    const int idx = juce::jlimit (0, windowN - 1, (int) std::lround (frac * windowN));
    const double ms = (double) idx / sampleRate * 1000.0;
    const float v = data[(size_t) juce::jmin ((int) data.size() - 1, idx)];

    g.setColour (t.text.withAlpha (0.25f));
    g.fillRect (std::round (x) - 0.5f, area.getY(), 1.0f, area.getHeight());

    const float cy = area.getCentreY();
    const float y = juce::jlimit (area.getY(), area.getBottom(), cy - v * area.getHeight() * 0.5f * ampScale * gain);
    g.setColour (t.accent);
    g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);

    const float db = juce::Decibels::gainToDecibels (std::abs (v), -100.0f);
    juce::String text;
    text << juce::String (ms, ms < 100.0 ? 2 : 1) << " ms   "
         << (v >= 0.0f ? "+" : "-") << juce::String (std::abs (v), 3)
         << "  (" << (std::abs (v) > 0.0f ? juce::String (db, 1) + " dB" : juce::String ("-inf")) << ")";

    g.setFont (juce::FontOptions (12.0f));
    auto box = juce::Rectangle<float> (area.getX() + 8.0f, area.getY() + 6.0f, 214.0f, 19.0f);
    g.setColour (t.scopeBg.withAlpha (0.88f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (t.outline);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    g.setColour (t.text);
    g.drawText (text, box.reduced (7.0f, 0.0f), juce::Justification::centredLeft);
}
