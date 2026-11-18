#include "ScopeView.h"

#include <cmath>
#include <iterator>
#include <numeric>

namespace
{
    constexpr float rulerHeight = 20.0f;
    constexpr float leftMargin  = 44.0f;   // room for the amplitude labels

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

    if (wc != lastWriteCounter || rc != lastResetCounter || lpOn != lastLpOn
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
    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs (src[i]));
    windowPeak = peak;

    processInto (src, n, wp, data, lpOn, lpHz);

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
    g.fillAll (ScopeColours::background);

    const auto ruler = getRulerArea();
    const auto trace = getTraceArea();
    const float gain = getGainLinear();

    g.setColour (ScopeColours::panel);
    g.fillRect (ruler);

    drawGrid (g, trace, ruler);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (trace.toNearestInt());

        if (holdActive && ! holdData.empty())
            drawTrace (g, holdData, trace, ScopeColours::hold.withAlpha (0.8f), gain, 1.7f);

        drawTrace (g, data, trace, ScopeColours::trace, gain, 1.7f);

        // Play cursor
        if (proc.isHostPlaying() && windowN > 0)
        {
            const double frac = ((double) writePos / (double) windowN - viewStart) / viewSpan;
            if (frac >= 0.0 && frac <= 1.0)
            {
                const float x = trace.getX() + (float) frac * trace.getWidth();
                g.setColour (ScopeColours::accent.withAlpha (0.55f));
                g.drawVerticalLine ((int) x, trace.getY(), trace.getBottom());
            }
        }
    }

    // Peak readout (top right of the trace area)
    {
        const bool clipping = windowPeak > 1.0f;
        const float peakDb = juce::Decibels::gainToDecibels (windowPeak, -100.0f);
        juce::String peakText = "Peak " + (windowPeak > 0.0f ? juce::String (peakDb, 1) + " dB" : juce::String ("-inf"));
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (clipping ? ScopeColours::clip : ScopeColours::textDim);
        g.drawText (peakText, trace.withHeight (16.0f).withTrimmedRight (8.0f).translated (0.0f, 2.0f),
                    juce::Justification::centredRight);
    }

    if (hovering && ! dragging)
        drawHover (g, trace, gain);

    // Thin frame
    g.setColour (ScopeColours::gridMid);
    g.drawRect (getLocalBounds().toFloat().withTrimmedLeft (leftMargin), 1.0f);
}

void ScopeView::drawGrid (juce::Graphics& g, juce::Rectangle<float> trace, juce::Rectangle<float> ruler)
{
    const float W = trace.getWidth();
    if (W <= 0.0f || windowQN <= 0.0)
        return;

    g.setFont (juce::FontOptions (11.0f));

    //--------------------------------------------------------------------------
    // Amplitude grid: linear scale, labelled in dB. 0 dB = full scale.
    const float gain = getGainLinear();
    const float cy = trace.getCentreY();
    const float halfH = trace.getHeight() * 0.5f;

    g.setColour (ScopeColours::centreLine);
    g.drawHorizontalLine ((int) cy, trace.getX(), trace.getRight());
    g.setColour (ScopeColours::textDim);
    g.drawText ("-inf", juce::Rectangle<float> (0.0f, cy - 7.0f, leftMargin - 6.0f, 14.0f), juce::Justification::centredRight);

    float lastOffset = 0.0f;
    for (int db = 0; db >= -60; db -= 6)
    {
        const float a = juce::Decibels::decibelsToGain ((float) db);
        const float offset = a * gain * halfH * 0.9f;
        if (offset > halfH - 2.0f)
            continue;
        if (offset < 16.0f || (lastOffset > 0.0f && lastOffset - offset < 16.0f))
            continue;
        lastOffset = offset;

        for (float sign : { -1.0f, 1.0f })
        {
            const float y = cy + sign * offset;
            g.setColour (db == 0 ? ScopeColours::gridMajor : ScopeColours::gridMinor);
            g.drawHorizontalLine ((int) y, trace.getX(), trace.getRight());
            g.setColour (ScopeColours::textDim);
            g.drawText (juce::String (db), juce::Rectangle<float> (0.0f, y - 7.0f, leftMargin - 6.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }

    //--------------------------------------------------------------------------
    // Time grid: musical subdivisions, or milliseconds when zoomed in very far
    const double visQN = viewSpan * windowQN;
    const double qnStart = viewStart * windowQN;
    const double pxPerQN = (double) W / visQN;

    auto xForQN = [&] (double qn) { return trace.getX() + (float) ((qn - qnStart) * pxPerQN); };

    constexpr double finestMusical = 4.0 / 128.0;   // a 1/128 note, in quarter notes
    const bool msMode = finestMusical * pxPerQN > 140.0 && windowN > 0 && sampleRate > 0.0;

    if (! msMode)
    {
        static const double steps[] = { 4.0 / 128.0, 4.0 / 64.0, 4.0 / 32.0, 4.0 / 16.0, 4.0 / 8.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
        double step = steps[std::size (steps) - 1];
        for (double s : steps)
            if (s * pxPerQN >= 64.0) { step = s; break; }

        const double first = std::ceil (qnStart / step - 1.0e-9) * step;
        for (double qn = first; qn <= qnStart + visQN + 1.0e-9; qn += step)
        {
            const float x = xForQN (qn);
            const bool isBar  = std::abs (std::remainder (qn, barQN)) < 1.0e-6;
            const bool isBeat = std::abs (std::remainder (qn, 1.0)) < 1.0e-6;

            g.setColour (isBar ? ScopeColours::gridMajor : (isBeat ? ScopeColours::gridMid : ScopeColours::gridMinor));
            g.drawVerticalLine ((int) x, trace.getY(), trace.getBottom());

            g.setColour (ScopeColours::gridMid);
            g.drawVerticalLine ((int) x, ruler.getBottom() - 5.0f, ruler.getBottom());

            if (x < ruler.getRight() - 20.0f)
            {
                g.setColour (isBeat ? ScopeColours::text : ScopeColours::textDim);
                g.drawText (formatWholeNoteFraction (qn), juce::Rectangle<float> (x + 3.0f, ruler.getY(), 70.0f, ruler.getHeight() - 2.0f),
                            juce::Justification::centredLeft);
            }
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

        const double first = std::ceil (msStart / step - 1.0e-9) * step;
        for (double ms = first; ms <= msStart + visMs + 1.0e-9; ms += step)
        {
            const float x = trace.getX() + (float) ((ms - msStart) * pxPerMs);
            g.setColour (ScopeColours::gridMinor);
            g.drawVerticalLine ((int) x, trace.getY(), trace.getBottom());
            g.setColour (ScopeColours::gridMid);
            g.drawVerticalLine ((int) x, ruler.getBottom() - 5.0f, ruler.getBottom());

            if (x < ruler.getRight() - 20.0f)
            {
                g.setColour (ScopeColours::textDim);
                g.drawText (formatMs (ms, step), juce::Rectangle<float> (x + 3.0f, ruler.getY(), 70.0f, ruler.getHeight() - 2.0f),
                            juce::Justification::centredLeft);
            }
        }

        // Musical lines still matter when zoomed in: draw any that are visible in accent colour
        const double first16 = std::ceil (qnStart / finestMusical - 1.0e-9) * finestMusical;
        for (double qn = first16; qn <= qnStart + visQN + 1.0e-9; qn += finestMusical)
        {
            g.setColour (ScopeColours::accent.withAlpha (0.35f));
            g.drawVerticalLine ((int) xForQN (qn), trace.getY(), trace.getBottom());
        }
    }
}

void ScopeView::drawTrace (juce::Graphics& g, const std::vector<float>& d, juce::Rectangle<float> area,
                           juce::Colour colour, float gain, float lineWidth)
{
    const int n = (int) d.size();
    if (n < 2 || area.getWidth() <= 0.0f)
        return;

    const float cy = area.getCentreY();
    const float scale = area.getHeight() * 0.5f * 0.9f * gain;
    const float top = area.getY() - 2.0f, bottom = area.getBottom() + 2.0f;

    auto yFor = [&] (float v) { return juce::jlimit (top, bottom, cy - v * scale); };

    const double startS = viewStart * n;
    const double visS   = viewSpan * n;

    const float pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int cols = juce::jmax (1, (int) std::ceil (area.getWidth() * pixelScale));
    const double samplesPerCol = visS / (double) cols;

    g.setColour (colour);

    if (samplesPerCol > 1.0)
    {
        // Dense: for every pixel column take the min and max, and join them in the order
        // they occur in time. Stroked as one smooth anti-aliased line, this keeps the
        // shape of each cycle readable (a kick looks like a kick, not a spiky bar chart)
        // while very dense content still fills in solidly.
        const int lcols = juce::jmax (1, (int) std::ceil (area.getWidth()));
        const double spc = visS / (double) lcols;
        const float colW = area.getWidth() / (float) lcols;

        juce::Path p;
        p.preallocateSpace (lcols * 6 + 6);
        bool started = false;

        auto addPoint = [&] (float x, float y)
        {
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else           p.lineTo (x, y);
        };

        for (int c = 0; c < lcols; ++c)
        {
            const int s0 = juce::jlimit (0, n - 1, (int) std::floor (startS + c * spc));
            const int s1 = juce::jlimit (0, n - 1, (int) std::floor (startS + (c + 1) * spc));

            int loIdx = s0, hiIdx = s0;
            for (int s = s0 + 1; s <= s1; ++s)
            {
                const float v = d[(size_t) s];
                if (v < d[(size_t) loIdx]) loIdx = s;
                if (v > d[(size_t) hiIdx]) hiIdx = s;
            }

            const float x0 = area.getX() + (float) c * colW;
            const int first  = juce::jmin (loIdx, hiIdx);
            const int second = juce::jmax (loIdx, hiIdx);

            addPoint (x0 + colW * (float) ((first  - startS - c * spc) / spc), yFor (d[(size_t) first]));
            if (second != first)
                addPoint (x0 + colW * (float) ((second - startS - c * spc) / spc), yFor (d[(size_t) second]));
        }

        g.strokePath (p, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        // Zoomed in: connect the individual samples, and show dots once they are far apart
        const int i0 = juce::jlimit (0, n - 1, (int) std::floor (startS) - 1);
        const int i1 = juce::jlimit (0, n - 1, (int) std::ceil (startS + visS) + 1);
        const double pxPerSample = (double) area.getWidth() / visS;

        auto xFor = [&] (int i) { return area.getX() + (float) (((double) i - startS) * pxPerSample); };

        juce::Path p;
        p.preallocateSpace ((i1 - i0 + 1) * 3);
        p.startNewSubPath (xFor (i0), yFor (d[(size_t) i0]));
        for (int i = i0 + 1; i <= i1; ++i)
            p.lineTo (xFor (i), yFor (d[(size_t) i]));

        g.strokePath (p, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (pxPerSample > 7.0)
        {
            const float r = pxPerSample > 16.0 ? 2.5f : 1.8f;
            for (int i = i0; i <= i1; ++i)
                g.fillEllipse (xFor (i) - r, yFor (d[(size_t) i]) - r, r * 2.0f, r * 2.0f);
        }
    }
}

void ScopeView::drawHover (juce::Graphics& g, juce::Rectangle<float> area, float gain)
{
    if (windowN <= 0 || data.empty() || area.getWidth() <= 0.0f)
        return;

    const float x = juce::jlimit (area.getX(), area.getRight(), hoverPos.x);
    const double frac = viewStart + (double) ((x - area.getX()) / area.getWidth()) * viewSpan;
    const int idx = juce::jlimit (0, windowN - 1, (int) std::lround (frac * windowN));
    const double ms = (double) idx / sampleRate * 1000.0;
    const float v = data[(size_t) juce::jmin ((int) data.size() - 1, idx)];

    g.setColour (ScopeColours::text.withAlpha (0.25f));
    g.drawVerticalLine ((int) x, area.getY(), area.getBottom());

    // Marker on the trace at the hovered sample
    const float cy = area.getCentreY();
    const float y = juce::jlimit (area.getY(), area.getBottom(), cy - v * area.getHeight() * 0.5f * 0.9f * gain);
    g.setColour (ScopeColours::accent);
    g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);

    const float db = juce::Decibels::gainToDecibels (std::abs (v), -100.0f);
    juce::String text;
    text << juce::String (ms, ms < 100.0 ? 2 : 1) << " ms   "
         << (v >= 0.0f ? "+" : "-") << juce::String (std::abs (v), 3)
         << "  (" << (std::abs (v) > 0.0f ? juce::String (db, 1) + " dB" : juce::String ("-inf")) << ")";

    g.setFont (juce::FontOptions (12.0f));
    const float tw = 210.0f;
    auto box = juce::Rectangle<float> (area.getX() + 8.0f, area.getY() + 4.0f, tw, 18.0f);
    g.setColour (ScopeColours::background.withAlpha (0.85f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (ScopeColours::text);
    g.drawText (text, box.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
}
