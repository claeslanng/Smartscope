#include "SettingsPanel.h"

namespace
{
    class PickerContent : public juce::Component,
                          private juce::ChangeListener
    {
    public:
        PickerContent (juce::Colour initial, std::function<void (juce::Colour)> cb)
            : selector (juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                        | juce::ColourSelector::showColourspace),
              onChange (std::move (cb))
        {
            selector.setCurrentColour (initial, juce::dontSendNotification);
            selector.addChangeListener (this);
            addAndMakeVisible (selector);
            setSize (250, 250);
        }

        ~PickerContent() override { selector.removeChangeListener (this); }

        void resized() override { selector.setBounds (getLocalBounds()); }

    private:
        juce::ColourSelector selector;
        std::function<void (juce::Colour)> onChange;

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (onChange)
                onChange (selector.getCurrentColour());
        }
    };

    void drawSection (juce::Graphics& g, const Theme& t, juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold).withKerningFactor (0.08f));
        g.drawText (text.toUpperCase(), r, juce::Justification::centredLeft);
    }
}

//==============================================================================
SettingsPanel::SettingsPanel (SmartscopeAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (true, true);

    doneButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (doneButton);

    for (int i = 0; i < numThemes; ++i)
        themeBox.addItem (themes[i].name, i + 1);
    themeBox.setJustificationType (juce::Justification::centred);
    themeBox.onChange = [this]
    {
        if (onThemeChanged)
            onThemeChanged (themeBox.getSelectedItemIndex());
        refresh();
    };
    addAndMakeVisible (themeBox);

    for (int layer = 0; layer < numLayers; ++layer)
    {
        auto& row = rows[layer];

        row.swatch.setTooltip ("Pick a colour for " + juce::String (UISettings::layerLongName (layer)));
        row.swatch.onClick = [this, layer]
        {
            openColourPicker (rows[layer].swatch, UISettings::colourKey (layer),
                              UISettings::layerColour (ui(), layer, theme()));
        };
        addAndMakeVisible (row.swatch);

        row.opacity.setSliderStyle (juce::Slider::LinearBar);
        row.opacity.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
        row.opacity.setRange (10.0, 100.0, 1.0);
        row.opacity.setTextValueSuffix (" %");
        row.opacity.setDoubleClickReturnValue (true, layer == 0 ? 100.0 : 85.0);
        row.opacity.onValueChange = [this, layer]
        {
            ui().setProperty (UISettings::opacityKey (layer), rows[layer].opacity.getValue() / 100.0, nullptr);
            changed();
        };
        addAndMakeVisible (row.opacity);

        row.visible.setTooltip ("Show or hide this layer");
        row.visible.onClick = [this, layer]
        {
            ui().setProperty (UISettings::visibleKey (layer), rows[layer].visible.getToggleState(), nullptr);
            changed();
        };
        addAndMakeVisible (row.visible);
    }

    holdSwatch.setTooltip ("Pick a colour for the held reference trace");
    holdSwatch.onClick = [this]
    {
        openColourPicker (holdSwatch, UISettings::holdColourKey, UISettings::holdColour (ui(), theme()));
    };
    addAndMakeVisible (holdSwatch);

    resetColoursButton.setTooltip ("Back to the default colours and opacity for every layer");
    resetColoursButton.onClick = [this]
    {
        auto tree = ui();
        for (int layer = 0; layer < numLayers; ++layer)
        {
            tree.removeProperty (UISettings::colourKey (layer), nullptr);
            tree.removeProperty (UISettings::opacityKey (layer), nullptr);
        }
        tree.removeProperty (UISettings::holdColourKey, nullptr);
        refresh();
        changed();
    };
    addAndMakeVisible (resetColoursButton);

    auto setupFlag = [this] (juce::ToggleButton& b, const juce::Identifier& key, bool fallback, const juce::String& tip)
    {
        b.setTooltip (tip);
        b.onClick = [this, &b, key] { ui().setProperty (key, b.getToggleState(), nullptr); changed(); };
        b.setToggleState (UISettings::flag (ui(), key, fallback), juce::dontSendNotification);
        addAndMakeVisible (b);
    };
    setupFlag (hoverToggle,   UISettings::showHoverKey,  true,  "Show time and value under the mouse");
    setupFlag (cursorToggle,  UISettings::showCursorKey, true,  "Show the play position line");
    setupFlag (clipToggle,    UISettings::showClipKey,   true,  "Red ticks where the signal goes above 0 dBFS");
    setupFlag (mainTopToggle, UISettings::mainOnTopKey,  false, "Draw the main input above the sidechain layers instead of below");

    refresh();
    startTimer (500);
}

SettingsPanel::~SettingsPanel()
{
    stopTimer();
}

void SettingsPanel::refresh()
{
    const auto& t = theme();
    auto tree = ui();

    themeBox.setSelectedItemIndex (juce::jlimit (0, numThemes - 1, proc.themeIndex.load()), juce::dontSendNotification);

    for (int layer = 0; layer < numLayers; ++layer)
    {
        auto& row = rows[layer];
        row.swatch.colour = UISettings::layerColour (tree, layer, t);
        row.swatch.outline = t.outline.brighter (0.3f);
        row.swatch.repaint();
        row.opacity.setValue (UISettings::layerOpacity (tree, layer) * 100.0, juce::dontSendNotification);
        row.visible.setToggleState (UISettings::layerVisible (tree, layer), juce::dontSendNotification);
    }

    holdSwatch.colour = UISettings::holdColour (tree, t);
    holdSwatch.outline = t.outline.brighter (0.3f);
    holdSwatch.repaint();

    hoverToggle.setToggleState   (UISettings::flag (tree, UISettings::showHoverKey,  true),  juce::dontSendNotification);
    cursorToggle.setToggleState  (UISettings::flag (tree, UISettings::showCursorKey, true),  juce::dontSendNotification);
    clipToggle.setToggleState    (UISettings::flag (tree, UISettings::showClipKey,   true),  juce::dontSendNotification);
    mainTopToggle.setToggleState (UISettings::flag (tree, UISettings::mainOnTopKey,  false), juce::dontSendNotification);

    repaint();
}

void SettingsPanel::changed()
{
    if (onSettingsChanged)
        onSettingsChanged();
}

void SettingsPanel::openColourPicker (juce::Component& anchor, const juce::Identifier& key, juce::Colour current)
{
    auto content = std::make_unique<PickerContent> (current, [this, key] (juce::Colour c)
    {
        ui().setProperty (key, c.toString(), nullptr);
        refresh();
        changed();
    });

    auto* parent = getParentComponent() != nullptr ? getParentComponent() : this;
    const auto area = parent->getLocalArea (&anchor, anchor.getLocalBounds());
    juce::CallOutBox::launchAsynchronously (std::move (content), area, parent);
}

//==============================================================================
void SettingsPanel::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.fillAll (t.window.withAlpha (0.7f));

    auto card = panelBounds.toFloat();
    g.setColour (t.window);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (t.outline.brighter (0.15f));
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);

    // Title
    auto inner = panelBounds.reduced (16, 12);
    g.setColour (t.text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Settings", inner.removeFromTop (26), juce::Justification::centredLeft);

    // Layers table
    drawSection (g, t, { layersArea.getX(), layersArea.getY(), layersArea.getWidth(), 18 }, "Layers");
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (t.textDim);
    const int headY = layersArea.getY() + 18;
    g.drawText ("Colour",  colSwatch,  headY, 60, 16, juce::Justification::centredLeft);
    g.drawText ("Opacity", colOpacity, headY, 80, 16, juce::Justification::centredLeft);
    g.drawText ("Show",    colShow,    headY, 40, 16, juce::Justification::centredLeft);

    for (int layer = 0; layer <= numLayers; ++layer)
    {
        const int y = rowTop + layer * rowHeight;
        const bool isHold = layer == numLayers;
        const juce::String name = isHold ? "Hold trace" : juce::String (UISettings::layerLongName (layer));
        const bool connected = isHold || proc.isLayerConnected (layer);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (connected ? t.text : t.textDim);
        g.drawText (name, colName, y, colSwatch - colName - 4, rowHeight / 2 + 4, juce::Justification::bottomLeft);

        g.setFont (juce::FontOptions (10.5f));
        g.setColour (t.textDim);
        juce::String sub;
        if (isHold)           sub = "snapshot";
        else if (layer == 0)  sub = "this track";
        else                  sub = connected ? "connected" : "not connected";
        g.drawText (sub, colName, y + rowHeight / 2 + 2, colSwatch - colName - 4, rowHeight / 2 - 2,
                    juce::Justification::topLeft);
    }

    // Right column
    auto r = rightArea;
    drawSection (g, t, r.removeFromTop (18), "Theme");
    r.removeFromTop (26 + 12);
    drawSection (g, t, r.removeFromTop (18), "Display");
    r.removeFromTop (2 * 24 + 12);
    drawSection (g, t, r.removeFromTop (18), "About");

    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (t.text);
    g.drawText ("Smartscope  " + juce::String ("v") + juce::String (SMARTSCOPE_VERSION), r.removeFromTop (18),
                juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.5f));
    g.setColour (t.accent);
    g.drawText ("by Claes Lanng Plugins", r.removeFromTop (17), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (11.5f));
    g.setColour (t.textDim);
    g.drawFittedText ("Sidechain layers: send other mixer tracks into Smartscope's sidechain inputs "
                      "1-3 to see them as coloured layers on top of this track.",
                      r.removeFromTop (46).withTrimmedTop (4), juce::Justification::topLeft, 3);
}

void SettingsPanel::resized()
{
    auto b = getLocalBounds().reduced (10, 8);
    const int w = juce::jmin (b.getWidth(), 820);
    const int h = juce::jmin (b.getHeight(), 300);
    panelBounds = juce::Rectangle<int> (0, 0, w, h).withCentre (b.getCentre());

    auto inner = panelBounds.reduced (16, 12);
    auto title = inner.removeFromTop (26);
    doneButton.setBounds (title.removeFromRight (70).reduced (0, 1));
    inner.removeFromTop (8);

    layersArea = inner.removeFromLeft ((int) (inner.getWidth() * 0.56f));
    inner.removeFromLeft (24);
    rightArea = inner;

    // Layers table
    colName    = layersArea.getX();
    colSwatch  = colName + 118;
    colOpacity = colSwatch + 52;
    colShow    = juce::jmin (layersArea.getRight() - 30, colOpacity + 140);
    const int opacityW = colShow - colOpacity - 14;
    rowTop = layersArea.getY() + 18 + 18;

    for (int layer = 0; layer < numLayers; ++layer)
    {
        const int y = rowTop + layer * rowHeight;
        rows[layer].swatch.setBounds (colSwatch, y + 4, 38, rowHeight - 8);
        rows[layer].opacity.setBounds (colOpacity, y + 3, opacityW, rowHeight - 6);
        rows[layer].visible.setBounds (colShow - 2, y + 2, 28, rowHeight - 4);
    }
    const int holdY = rowTop + numLayers * rowHeight;
    holdSwatch.setBounds (colSwatch, holdY + 4, 38, rowHeight - 8);
    resetColoursButton.setBounds (colOpacity, holdY + 3, opacityW, rowHeight - 6);

    // Right column
    auto r = rightArea;
    r.removeFromTop (18);
    themeBox.setBounds (r.removeFromTop (26).withWidth (juce::jmin (180, r.getWidth())));
    r.removeFromTop (12 + 18);
    auto row1 = r.removeFromTop (24);
    auto row2 = r.removeFromTop (24);
    hoverToggle.setBounds (row1.removeFromLeft (row1.getWidth() / 2));
    cursorToggle.setBounds (row1);
    clipToggle.setBounds (row2.removeFromLeft (row2.getWidth() / 2));
    mainTopToggle.setBounds (row2);
}
