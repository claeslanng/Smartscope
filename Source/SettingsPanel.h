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
/** "Random colors" button with a little die drawn next to the text. */
class DiceButton : public juce::Button
{
public:
    DiceButton() : juce::Button ("Random colors") {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

//==============================================================================
/** Overlay with themes, layer colours/opacity/visibility, display options and about info. */
class SettingsPanel : public juce::Component,
                      private juce::Timer
{
public:
    /** The smallest area the panel can lay itself out in. The editor closes it below this. */
    static constexpr int minWidth = 760, minHeight = 316;

    explicit SettingsPanel (SmartscopeAudioProcessor&);
    ~SettingsPanel() override;

    /** Give sidechains 1-3 new random colours (the dice button). */
    void randomiseSidechainColours();

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
        juce::TextEditor hex;
        juce::Slider opacity;
        juce::ToggleButton visible;
    };
    LayerRow rows[numLayers];
    ColourSwatch holdSwatch;
    juce::TextEditor holdHex;
    DiceButton randomButton;
    juce::TextButton resetColoursButton { "Reset colors" };
    juce::Random rng;

    juce::ToggleButton hoverToggle   { "Hover readout" };
    juce::ToggleButton cursorToggle  { "Play cursor" };
    juce::ToggleButton clipToggle    { "0 dB markers" };
    juce::ToggleButton mainTopToggle { "Main layer on top" };

    juce::Rectangle<int> panelBounds, layersArea, rightArea;
    int rowTop = 0, rowHeight = 28;
    int colName = 0, colSwatch = 0, colHex = 0, colOpacity = 0, colShow = 0;

    const Theme& theme() const { return getTheme (proc.themeIndex.load()); }
    juce::ValueTree ui() { return proc.getUISettings(); }

    void openColourPicker (juce::Component& anchor, const juce::Identifier& key, juce::Colour current);
    void setupHexEditor (juce::TextEditor&, const juce::Identifier& key, const juce::String& what);
    void commitHex (juce::TextEditor&, const juce::Identifier& key);
    juce::Colour currentColour (const juce::Identifier& key);
    void changed();
    void timerCallback() override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};
