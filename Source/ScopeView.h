#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

#include <functional>
#include <vector>

namespace ScopeColours
{
    const juce::Colour background   { 0xff141517 };
    const juce::Colour panel        { 0xff1d1f22 };
    const juce::Colour gridMajor    { 0xff3a3d42 };
    const juce::Colour gridMid      { 0xff2c2f33 };
    const juce::Colour gridMinor    { 0xff222428 };
    const juce::Colour centreLine   { 0xff5a5e64 };
    const juce::Colour text         { 0xffb8bcc2 };
    const juce::Colour textDim      { 0xff70757c };
    const juce::Colour trace        { 0xffd4d4d6 };
    const juce::Colour accent       { 0xfff08a24 };
    const juce::Colour hold         { 0xff3fb6ff };
    const juce::Colour clip         { 0xffff4a4a };
}

//==============================================================================
/** The waveform display: draws the processor's window buffer, handles zooming,
    scrolling, the display-only low-pass filter and the held reference trace. */
class ScopeView : public juce::Component,
                  private juce::Timer
{
public:
    explicit ScopeView (SmartscopeAudioProcessor&);
    ~ScopeView() override;

    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    void setHold (bool shouldHold);
    bool isHolding() const noexcept { return holdActive; }

    void resetZoom();
    double getZoomFactor() const noexcept { return 1.0 / viewSpan; }

    /** Called whenever zoom or hold state changes (for the editor's labels). */
    std::function<void()> onViewChanged;

   #if SMARTSCOPE_TEST_BUILD
    void setViewForTest (double start, double span) { viewStart = start; viewSpan = span; clampView(); notifyViewChanged(); }
    void setHoverForTest (juce::Point<float> p)     { hoverPos = p; hovering = true; }
    void refreshForTest()                           { rebuild(); }
   #endif

private:
    SmartscopeAudioProcessor& proc;

    // View, as a fraction of the window (0..1)
    double viewStart = 0.0;
    double viewSpan  = 1.0;

    // Snapshot of the processor's buffer (filtered if the low-pass is on), in window order
    std::vector<float> data;
    int    windowN = 0;
    int    writePos = 0;
    float  windowPeak = 0.0f;
    double sampleRate = 44100.0;
    double windowQN = 2.0;
    double barQN = 4.0;

    // Held reference trace
    bool holdActive = false;
    std::vector<float> holdRaw, holdData;
    int holdWritePos = 0;

    // Change detection
    uint32_t lastWriteCounter = 0, lastResetCounter = 0;
    bool  lastLpOn = false;
    float lastLpHz = 0.0f;
    bool  needsRebuild = true;

    // Mouse
    juce::Point<float> hoverPos;
    bool hovering = false;
    bool dragging = false;
    double dragStartView = 0.0;
    float  wheelAccumulator = 0.0f;

    std::vector<double> filterScratch;

    juce::Rectangle<float> getRulerArea() const;
    juce::Rectangle<float> getTraceArea() const;

    void timerCallback() override;
    void rebuild();
    void processInto (const float* src, int n, int wp, std::vector<float>& dest, bool lpOn, float lpHz);
    void zeroPhaseLowpass (double* x, int n, double cutoff);

    void zoomAround (double xFraction, double factor);
    void clampView();
    void notifyViewChanged();

    float getGainLinear() const;
    void  nudgeGainDb (float deltaDb);
    void  stepLength (int delta);

    void drawGrid (juce::Graphics&, juce::Rectangle<float> trace, juce::Rectangle<float> ruler);
    void drawTrace (juce::Graphics&, const std::vector<float>& d, juce::Rectangle<float> area,
                    juce::Colour colour, float gain, float lineWidth);
    void drawHover (juce::Graphics&, juce::Rectangle<float> area, float gain);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeView)
};
