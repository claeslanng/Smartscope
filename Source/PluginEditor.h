#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ScopeView.h"

//==============================================================================
class SmartscopeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SmartscopeLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
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

private:
    SmartscopeAudioProcessor& proc;
    SmartscopeLookAndFeel lnf;

    ScopeView scope;

    juce::TextButton lengthDown { "<" }, lengthUp { ">" };
    juce::ComboBox   lengthBox;
    juce::Slider     gainSlider;
    juce::TextButton lowpassButton { "Low-pass" };
    juce::Slider     lowpassSlider;
    juce::TextButton holdButton { "Hold" };
    juce::TextButton resetZoomButton { "Reset zoom" };

    juce::String statusText, zoomText;

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment>  lengthAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment, lowpassHzAttachment;
    std::unique_ptr<ButtonAttachment> lowpassAttachment;

    juce::Rectangle<int> headerArea, footerArea;

    void timerCallback() override;
    void updateZoomText();
    void stepLength (int delta);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartscopeAudioProcessorEditor)
};
