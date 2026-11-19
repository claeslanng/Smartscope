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

        row.swatch.setTooltip ("Pick a color for " + juce::String (UISettings::layerLongName (layer)));
        row.swatch.onClick = [this, layer]
        {
            openColourPicker (rows[layer].swatch, UISettings::colourKey (layer),
                              UISettings::layerColour (ui(), layer, theme()));
        };
        addAndMakeVisible (row.swatch);

        setupHexEditor (row.hex, UISettings::colourKey (layer), UISettings::layerLongName (layer));

        row.opacity.setSliderStyle (juce::Slider::LinearBar);
        row.opacity.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
        row.opacity.setRange (10.0, 100.0, 1.0);
        row.opacity.setTextValueSuffix (" %");
        row.opacity.setDoubleClickReturnValue (true, UISettings::defaultOpacity * 100.0);
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

    holdSwatch.setTooltip ("Pick a color for the held reference trace");
    holdSwatch.onClick = [this]
    {
        openColourPicker (holdSwatch, UISettings::holdColourKey, UISettings::holdColour (ui(), theme()));
    };
    addAndMakeVisible (holdSwatch);
    setupHexEditor (holdHex, UISettings::holdColourKey, "the held reference trace");

    randomButton.setTooltip ("New random colors for sidechains 1-3. They stay easy to see on the current theme "
                             "and easy to tell apart. Click again for another set.");
    randomButton.onClick = [this] { randomiseSidechainColours(); };
    addAndMakeVisible (randomButton);

    resetColoursButton.setTooltip ("Back to the default colors and opacity for every layer");
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

    // Keep a code that was typed but not confirmed with Enter, then detach the callbacks:
    // the editors lose focus while they are being destroyed, after this panel is half gone.
    auto finish = [this] (juce::TextEditor& ed, const juce::Identifier& key)
    {
        juce::Colour c;
        if (ed.hasKeyboardFocus (true) && UISettings::parseHex (ed.getText(), c) && c != currentColour (key))
            ui().setProperty (key, c.toString(), nullptr);
        ed.onFocusLost = nullptr;
        ed.onReturnKey = nullptr;
        ed.onEscapeKey = nullptr;
    };
    for (int layer = 0; layer < numLayers; ++layer)
        finish (rows[layer].hex, UISettings::colourKey (layer));
    finish (holdHex, UISettings::holdColourKey);
}

juce::Colour SettingsPanel::currentColour (const juce::Identifier& key)
{
    if (key == UISettings::holdColourKey)
        return UISettings::holdColour (ui(), theme());

    for (int layer = 0; layer < numLayers; ++layer)
        if (key == UISettings::colourKey (layer))
            return UISettings::layerColour (ui(), layer, theme());

    return juce::Colours::white;
}

void SettingsPanel::setupHexEditor (juce::TextEditor& ed, const juce::Identifier& key, const juce::String& what)
{
    ed.setInputRestrictions (7, "#0123456789abcdefABCDEF");
    ed.setJustification (juce::Justification::centred);
    ed.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
    ed.setIndents (2, 3);
    ed.setSelectAllWhenFocused (true);
    ed.setTooltip ("Hex color code for " + what + ", as #RRGGBB. Type or paste a code and press Enter.");
    ed.onReturnKey = [this, &ed, key] { commitHex (ed, key); ed.giveAwayKeyboardFocus(); };
    ed.onFocusLost = [this, &ed, key] { commitHex (ed, key); };
    ed.onEscapeKey = [this, &ed, key]
    {
        ed.setText (UISettings::toHex (currentColour (key)), juce::dontSendNotification);
        ed.giveAwayKeyboardFocus();
    };
    addAndMakeVisible (ed);
}

void SettingsPanel::commitHex (juce::TextEditor& ed, const juce::Identifier& key)
{
    juce::Colour c;
    if (UISettings::parseHex (ed.getText(), c))
    {
        if (c != currentColour (key))
        {
            ui().setProperty (key, c.toString(), nullptr);
            changed();
        }
    }
    // Valid or not, show the colour that is actually in use, in the standard #RRGGBB form
    ed.setText (UISettings::toHex (currentColour (key)), juce::dontSendNotification);
    refresh();
}

void SettingsPanel::randomiseSidechainColours()
{
    const auto& t = theme();
    auto tree = ui();
    const auto picks = UISettings::randomSidechainColours (t, rng, UISettings::layerColour (tree, 0, t),
                                                           UISettings::holdColour (tree, t));
    for (int layer = 1; layer < numLayers; ++layer)
        tree.setProperty (UISettings::colourKey (layer), picks[(size_t) (layer - 1)].toString(), nullptr);

    refresh();
    changed();
}

void DiceButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto& lf = getLookAndFeel();
    lf.drawButtonBackground (g, *this, juce::Colours::transparentBlack, over, down);

    const auto textColour = findColour (juce::TextButton::textColourOffId);
    auto r = getLocalBounds().reduced (8, 0);

    g.setFont (juce::FontOptions (13.0f));
    const int textW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), getName()));
    const int dieSize = juce::jmin (14, getHeight() - 8);
    const int total = dieSize + 7 + textW;
    auto content = r.withSizeKeepingCentre (juce::jmin (total, r.getWidth()), getHeight());

    // The die: rounded square with five pips
    auto die = content.removeFromLeft (dieSize).withSizeKeepingCentre (dieSize, dieSize).toFloat();
    g.setColour (textColour);
    g.drawRoundedRectangle (die.reduced (0.5f), 2.5f, 1.2f);
    const float p = juce::jmax (1.6f, dieSize * 0.15f);
    auto pip = [&] (float fx, float fy)
    {
        g.fillEllipse (die.getX() + die.getWidth() * fx - p * 0.5f, die.getY() + die.getHeight() * fy - p * 0.5f, p, p);
    };
    pip (0.28f, 0.28f); pip (0.72f, 0.28f); pip (0.5f, 0.5f); pip (0.28f, 0.72f); pip (0.72f, 0.72f);

    content.removeFromLeft (7);
    g.drawText (getName(), content, juce::Justification::centredLeft, false);
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
        if (! row.hex.hasKeyboardFocus (true))
        {
            row.hex.applyColourToAllText (t.text);
            row.hex.setText (UISettings::toHex (row.swatch.colour), juce::dontSendNotification);
        }
        row.opacity.setValue (UISettings::layerOpacity (tree, layer) * 100.0, juce::dontSendNotification);
        row.visible.setToggleState (UISettings::layerVisible (tree, layer), juce::dontSendNotification);
    }

    holdSwatch.colour = UISettings::holdColour (tree, t);
    holdSwatch.outline = t.outline.brighter (0.3f);
    holdSwatch.repaint();
    if (! holdHex.hasKeyboardFocus (true))
    {
        holdHex.applyColourToAllText (t.text);
        holdHex.setText (UISettings::toHex (holdSwatch.colour), juce::dontSendNotification);
    }

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
    g.drawText ("Color",  colSwatch,  headY, 60, 16, juce::Justification::centredLeft);
    g.drawText ("Hex",     colHex + 2, headY, 60, 16, juce::Justification::centredLeft);
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
                      "1-3 to see them as colored layers on top of this track.",
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

    layersArea = inner.removeFromLeft (juce::jmin (440, (int) (inner.getWidth() * 0.58f)));
    inner.removeFromLeft (24);
    rightArea = inner;

    // Layers table: name | swatch | hex | opacity | show
    constexpr int swatchW = 30, hexW = 74;
    colName    = layersArea.getX();
    colSwatch  = colName + 110;
    colHex     = colSwatch + swatchW + 6;
    colOpacity = colHex + hexW + 10;
    colShow    = layersArea.getRight() - 26;
    const int opacityW = juce::jmax (40, colShow - colOpacity - 12);
    rowTop = layersArea.getY() + 18 + 18;

    for (int layer = 0; layer < numLayers; ++layer)
    {
        const int y = rowTop + layer * rowHeight;
        rows[layer].swatch.setBounds (colSwatch, y + 4, swatchW, rowHeight - 8);
        rows[layer].hex.setBounds (colHex, y + 3, hexW, rowHeight - 6);
        rows[layer].opacity.setBounds (colOpacity, y + 3, opacityW, rowHeight - 6);
        rows[layer].visible.setBounds (colShow - 2, y + 2, 28, rowHeight - 4);
    }
    const int holdY = rowTop + numLayers * rowHeight;
    holdSwatch.setBounds (colSwatch, holdY + 4, swatchW, rowHeight - 8);
    holdHex.setBounds (colHex, holdY + 3, hexW, rowHeight - 6);

    const int buttonsY = holdY + rowHeight + 4;
    randomButton.setBounds (colName, buttonsY, 146, rowHeight - 4);
    resetColoursButton.setBounds (colName + 146 + 8, buttonsY, 118, rowHeight - 4);

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
