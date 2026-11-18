#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 34;
    constexpr int footerHeight = 46;
}

//==============================================================================
SmartscopeLookAndFeel::SmartscopeLookAndFeel()
{
    using namespace ScopeColours;
    setColour (juce::ResizableWindow::backgroundColourId, panel);
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, gridMid);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, accent);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
}

void SmartscopeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                  bool isHighlighted, bool isDown)
{
    using namespace ScopeColours;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = background;
    if (isDown)             fill = fill.brighter (0.15f);
    else if (isHighlighted) fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (b.getToggleState() ? accent.withAlpha (0.8f) : gridMid);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void SmartscopeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                  : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Font SmartscopeLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::FontOptions (13.0f);
}

void SmartscopeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float, float,
                                              juce::Slider::SliderStyle style, juce::Slider& s)
{
    using namespace ScopeColours;
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, s);
        return;
    }

    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
    g.setColour (background);
    g.fillRoundedRectangle (r, 3.0f);

    auto filled = r.withRight (juce::jlimit (r.getX(), r.getRight(), sliderPos));
    g.setColour (accent.withAlpha (s.isEnabled() ? 0.28f : 0.12f));
    g.fillRoundedRectangle (filled, 3.0f);

    g.setColour (gridMid);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void SmartscopeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                          juce::ComboBox& box)
{
    using namespace ScopeColours;
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (background);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (true) ? accent.withAlpha (0.6f) : gridMid);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

juce::Font SmartscopeLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (13.0f);
}

juce::Label* SmartscopeLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (13.0f));
    l->setColour (juce::Label::textColourId, ScopeColours::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

//==============================================================================
SmartscopeAudioProcessorEditor::SmartscopeAudioProcessorEditor (SmartscopeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), scope (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (scope);
    scope.onViewChanged = [this] { updateZoomText(); };

    // Length
    for (int i = 0; i < numLengthChoices; ++i)
        lengthBox.addItem (lengthChoices[i].name, i + 1);
    lengthBox.setJustificationType (juce::Justification::centred);
    lengthBox.setTooltip ("Window length (Alt + mouse wheel over the display)");
    addAndMakeVisible (lengthBox);
    lengthAttachment = std::make_unique<ComboAttachment> (proc.apvts, ParamIDs::length, lengthBox);

    lengthDown.onClick = [this] { stepLength (-1); };
    lengthUp.onClick   = [this] { stepLength (1); };
    addAndMakeVisible (lengthDown);
    addAndMakeVisible (lengthUp);

    // Gain
    gainSlider.setSliderStyle (juce::Slider::LinearBar);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
    gainSlider.setTooltip ("Display gain (Ctrl + mouse wheel over the display). Does not change the audio.");
    addAndMakeVisible (gainSlider);
    gainAttachment = std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::gain, gainSlider);
    gainSlider.setDoubleClickReturnValue (true, 0.0);

    // Low-pass
    lowpassButton.setClickingTogglesState (true);
    lowpassButton.setTooltip ("Display-only low-pass filter, to see just the kick and bass. Does not change the audio.");
    addAndMakeVisible (lowpassButton);
    lowpassAttachment = std::make_unique<ButtonAttachment> (proc.apvts, ParamIDs::lowpassOn, lowpassButton);

    lowpassSlider.setSliderStyle (juce::Slider::LinearBar);
    lowpassSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
    addAndMakeVisible (lowpassSlider);
    lowpassHzAttachment = std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::lowpassHz, lowpassSlider);
    lowpassSlider.setDoubleClickReturnValue (true, 150.0);

    // Hold
    holdButton.setClickingTogglesState (true);
    holdButton.setTooltip ("Keep a snapshot of the current window as a blue reference trace");
    holdButton.onClick = [this] { scope.setHold (holdButton.getToggleState()); };
    addAndMakeVisible (holdButton);

    resetZoomButton.onClick = [this] { scope.resetZoom(); };
    addAndMakeVisible (resetZoomButton);

    setResizable (true, true);
    setResizeLimits (760, 360, 3000, 1600);
    setSize (juce::jlimit (760, 3000, proc.editorWidth), juce::jlimit (360, 1600, proc.editorHeight));

    updateZoomText();
    timerCallback();
    startTimerHz (10);
}

SmartscopeAudioProcessorEditor::~SmartscopeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SmartscopeAudioProcessorEditor::stepLength (int delta)
{
    const int next = juce::jlimit (0, numLengthChoices - 1, lengthBox.getSelectedItemIndex() + delta);
    lengthBox.setSelectedItemIndex (next, juce::sendNotificationSync);
}

void SmartscopeAudioProcessorEditor::updateZoomText()
{
    const double z = scope.getZoomFactor();
    zoomText = "Zoom x" + (z < 10.0 ? juce::String (z, 1) : juce::String (juce::roundToInt (z)));
    resetZoomButton.setEnabled (z > 1.0001);
    repaint (footerArea);
}

void SmartscopeAudioProcessorEditor::timerCallback()
{
    const double bpm = proc.getBpm();
    const double windowMs = proc.getDisplaySampleRate() > 0.0
                              ? (double) proc.getWindowSamples() / proc.getDisplaySampleRate() * 1000.0 : 0.0;

    juce::String s;
    s << (proc.isSynced() ? "SYNCED" : "FREE") << "   "
      << juce::String (bpm, 1) << " BPM   "
      << "window " << juce::String (windowMs, windowMs < 1000.0 ? 1 : 0) << " ms";
    if (proc.isClamped())
        s << "  (shortened: tempo too slow)";

    if (s != statusText)
    {
        statusText = s;
        repaint (headerArea);
    }

    lowpassSlider.setAlpha (lowpassButton.getToggleState() ? 1.0f : 0.55f);
}

//==============================================================================
void SmartscopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace ScopeColours;
    g.fillAll (panel);

    // Header
    {
        auto r = headerArea.reduced (12, 0);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.setColour (juce::Colours::white);
        const juce::String a = "Smart", b = "scope";
        juce::GlyphArrangement ga;
        ga.addLineOfText (g.getCurrentFont(), a, 0.0f, 0.0f);
        const int aw = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 1;
        g.drawText (a, r.removeFromLeft (aw), juce::Justification::centredLeft);
        g.setColour (accent);
        g.drawText (b, r.removeFromLeft (70), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.0f));
        g.setColour (proc.isSynced() ? accent : textDim);
        g.drawText (statusText, r.removeFromLeft (360).withTrimmedLeft (14), juce::Justification::centredLeft);

        g.setColour (textDim);
        g.drawText ("Wheel: zoom   Shift: scroll   Ctrl: gain   Alt: length   Drag: pan   Double-click: reset",
                    r, juce::Justification::centredRight);
    }

    // Footer labels
    {
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (textDim);
        auto labelLeftOf = [&] (juce::Component& c, const juce::String& text)
        {
            g.drawText (text, juce::Rectangle<int> (c.getX() - 52, c.getY(), 46, c.getHeight()),
                        juce::Justification::centredRight);
        };
        labelLeftOf (lengthDown, "Length");
        labelLeftOf (gainSlider, "Gain");

        g.setColour (text);
        g.drawText (zoomText, juce::Rectangle<int> (resetZoomButton.getX() - 80, resetZoomButton.getY(), 72, resetZoomButton.getHeight()),
                    juce::Justification::centredRight);
    }
}

void SmartscopeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (headerHeight);
    footerArea = r.removeFromBottom (footerHeight);

    scope.setBounds (r.reduced (8, 0));

    auto f = footerArea.reduced (8, 9);
    f.removeFromLeft (52);
    lengthDown.setBounds (f.removeFromLeft (26));
    f.removeFromLeft (3);
    lengthBox.setBounds (f.removeFromLeft (84));
    f.removeFromLeft (3);
    lengthUp.setBounds (f.removeFromLeft (26));

    f.removeFromLeft (60);
    gainSlider.setBounds (f.removeFromLeft (110));

    f.removeFromLeft (20);
    lowpassButton.setBounds (f.removeFromLeft (80));
    f.removeFromLeft (3);
    lowpassSlider.setBounds (f.removeFromLeft (100));

    f.removeFromLeft (20);
    holdButton.setBounds (f.removeFromLeft (60));

    resetZoomButton.setBounds (f.removeFromRight (90));

    proc.editorWidth = getWidth();
    proc.editorHeight = getHeight();
}
