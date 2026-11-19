#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Themes.h"
#include "UISettings.h"

#include <functional>
#include <vector>

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
    juce::Rectangle<float> getTraceAreaForTest() const { return getTraceArea(); }
   #endif

private:
    SmartscopeAudioProcessor& proc;

    // View, as a fraction of the window (0..1)
    double viewStart = 0.0;
    double viewSpan  = 1.0;

    // Snapshot of the processor's buffer (filtered if the low-pass is on), in window order
    std::vector<float> layerData[numLayers];
    bool layerShown[numLayers] { true, false, false, false };   // connected (snapshot)
    std::vector<float>& data = layerData[0];
    int    windowN = 0;
    int    writePos = 0;
    float  windowPeak = 0.0f;
    std::vector<std::pair<int, int>> clipRuns;   // sample ranges above 0 dBFS (raw signal)
    double sampleRate = 44100.0;
    double windowQN = 2.0;
    double barQN = 4.0;

    // Held reference trace
    bool holdActive = false;
    std::vector<float> holdRaw, holdData;
    int holdWritePos = 0;

    // Change detection
    uint32_t lastWriteCounter = 0, lastResetCounter = 0;
    bool lastConnected[numLayers] { true, false, false, false };
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
    std::vector<float> envHi, envLo;

    const Theme& theme() const { return getTheme (proc.themeIndex.load()); }

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
                    juce::Colour colour, float gain, float lineWidth, float glow);
    void drawClipMarkers (juce::Graphics&, juce::Rectangle<float> area);
    void drawHover (juce::Graphics&, juce::Rectangle<float> area, float gain);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeView)
};
