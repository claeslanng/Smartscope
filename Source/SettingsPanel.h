#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "Themes.h"
#include "UISettings.h"

//==============================================================================
/** A small button that shows a colour and opens a colour picker. */
class ColourSwatch : public juce::Button
{
public:
    ColourSwatch() : juce::Button ("swatch") {}
    juce::Colour colour;
    juce::Colour outline { 0xff555555 };

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (over ? outline.brighter (0.6f) : outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
};

//==============================================================================
/** Overlay with themes, layer colours/opacity/visibility, display options and about info. */
class SettingsPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit SettingsPanel (SmartscopeAudioProcessor&);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks so the scope underneath doesn't react

    /** Refresh every control from the stored settings (e.g. after a theme change). */
    void refresh();

    std::function<void (int)> onThemeChanged;
    std::function<void()>     onSettingsChanged;
    std::function<void()>     onClose;

private:
    SmartscopeAudioProcessor& proc;

    juce::TextButton doneButton { "Done" };
    juce::ComboBox   themeBox;

    struct LayerRow
    {
        ColourSwatch swatch;
        juce::Slider opacity;
        juce::ToggleButton visible;
    };
    LayerRow rows[numLayers];
    ColourSwatch holdSwatch;
    juce::TextButton resetColoursButton { "Reset colours" };

    juce::ToggleButton hoverToggle   { "Hover readout" };
    juce::ToggleButton cursorToggle  { "Play cursor" };
    juce::ToggleButton clipToggle    { "0 dB markers" };
    juce::ToggleButton mainTopToggle { "Main layer on top" };

    juce::Rectangle<int> panelBounds, layersArea, rightArea;
    int rowTop = 0, rowHeight = 28;
    int colName = 0, colSwatch = 0, colOpacity = 0, colShow = 0;

    const Theme& theme() const { return getTheme (proc.themeIndex.load()); }
    juce::ValueTree ui() { return proc.getUISettings(); }

    void openColourPicker (juce::Component& anchor, const juce::Identifier& key, juce::Colour current);
    void changed();
    void timerCallback() override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};
