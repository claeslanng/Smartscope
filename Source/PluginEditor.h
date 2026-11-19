#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ScopeView.h"
#include "Themes.h"
#include "UISettings.h"
#include "SettingsPanel.h"

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
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

private:
    const Theme* theme = &themes[0];
    juce::TextLayout layoutTooltip (const juce::String&) const;
};

//==============================================================================
/** Header chip for one layer: coloured dot + name. Click toggles visibility. */
class LayerChip : public juce::Button
{
public:
    LayerChip (SmartscopeAudioProcessor& p, int layerIndex)
        : juce::Button (UISettings::layerName (layerIndex)), proc (p), layer (layerIndex) {}

    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    void clicked() override;
    void updateTooltip();

    std::function<void()> onToggled;

private:
    SmartscopeAudioProcessor& proc;
    int layer;
};

/** Small icon buttons (gear / question mark / scope-only in and out) drawn as vector shapes. */
class IconButton : public juce::Button
{
public:
    enum class Icon { gear, help, expand, collapse };
    IconButton (Icon i) : juce::Button ("icon"), icon (i) {}
    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;

private:
    Icon icon;
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
    void toggleSettings();
    bool isSettingsOpen() const noexcept         { return settingsPanel != nullptr; }
    SettingsPanel* getSettingsPanel() noexcept   { return settingsPanel.get(); }

    /** Scope-only view: hides the header and footer so only the oscilloscope shows.
        Saved with the project. */
    void setScopeOnly (bool shouldBeScopeOnly);
    bool isScopeOnly() const;

    /** Smallest and largest window sizes. */
    static constexpr int minWidth = 240, minHeight = 110, maxWidth = 7680, maxHeight = 4320;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;

private:
    SmartscopeAudioProcessor& proc;
    SmartscopeLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };

    ScopeView scope;

    IconButton settingsButton  { IconButton::Icon::gear };
    IconButton helpButton      { IconButton::Icon::help };
    IconButton scopeOnlyButton { IconButton::Icon::expand };
    IconButton exitScopeOnly   { IconButton::Icon::collapse };   // floats over the scope in scope-only view
    juce::uint32 lastMouseMoveMs = 0;
    std::unique_ptr<LayerChip> chips[numLayers];
    std::unique_ptr<SettingsPanel> settingsPanel;

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

    juce::Rectangle<int> headerArea, footerArea, statusArea, logoArea;
    juce::Rectangle<int> lengthLabelArea, gainLabelArea;

    void timerCallback() override;
    void layoutHeader (juce::Rectangle<int>);
    void layoutFooter (juce::Rectangle<int>);
    void updateExitButton();
    bool settingsFit() const;
    void updateZoomText();
    void stepLength (int delta);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartscopeAudioProcessorEditor)
};
