#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ScopeView.h"
#include "Themes.h"

//==============================================================================
class SmartscopeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SmartscopeLookAndFeel();

    void setTheme (const Theme&);
    const Theme& getThemeColours() const noexcept { return *theme; }

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

private:
    const Theme* theme = &themes[0];
    juce::TextLayout layoutTooltip (const juce::String&) const;
};

//==============================================================================
class SmartscopeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit SmartscopeAudioProcessorEditor (SmartscopeAudioProcessor&);
    ~SmartscopeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    ScopeView& getScopeView() noexcept { return scope; }
    void applyTheme (int index);

private:
    SmartscopeAudioProcessor& proc;
    SmartscopeLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };

    ScopeView scope;

    juce::ComboBox   themeBox;
    juce::TextButton helpButton { "?" };

    juce::TextButton lengthDown { "<" }, lengthUp { ">" };
    juce::ComboBox   lengthBox;
    juce::Slider     gainSlider;
    juce::TextButton lowpassButton { "Low-pass" };
    juce::Slider     lowpassSlider;
    juce::TextButton holdButton { "Hold" };
    juce::TextButton zoomButton { "Zoom x1.0" };

    juce::String statusText;
    bool statusSynced = false;

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment>  lengthAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment, lowpassHzAttachment;
    std::unique_ptr<ButtonAttachment> lowpassAttachment;

    juce::Rectangle<int> headerArea, footerArea, statusArea;
    juce::Rectangle<int> lengthLabelArea, gainLabelArea;

    void timerCallback() override;
    void updateZoomText();
    void stepLength (int delta);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartscopeAudioProcessorEditor)
};
