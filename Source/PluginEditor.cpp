#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 36;
    constexpr int footerHeight = 46;

    const char* helpText =
        "Mouse over the display\n"
        "Wheel: zoom in/out at the mouse\n"
        "Shift + wheel: scroll\n"
        "Ctrl + wheel: display gain\n"
        "Alt + wheel: window length\n"
        "Drag: pan    Double-click: reset zoom\n"
        "\n"
        "Red ticks at the top mark where the signal goes above 0 dBFS.";
}

//==============================================================================
SmartscopeLookAndFeel::SmartscopeLookAndFeel()
{
    setTheme (themes[0]);
}

void SmartscopeLookAndFeel::setTheme (const Theme& t)
{
    theme = &t;
    setColour (juce::ResizableWindow::backgroundColourId, t.window);
    setColour (juce::ComboBox::backgroundColourId, t.control);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.outline);
    setColour (juce::ComboBox::arrowColourId, t.textDim);
    setColour (juce::PopupMenu::backgroundColourId, t.window);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, t.text);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, t.accent);
    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, t.accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, t.text);
    setColour (juce::TooltipWindow::backgroundColourId, t.window);
    setColour (juce::TooltipWindow::textColourId, t.text);
    setColour (juce::TooltipWindow::outlineColourId, t.outline);
    setColour (juce::ToggleButton::textColourId, t.text);
    setColour (juce::ToggleButton::tickColourId, t.accent);
    setColour (juce::ToggleButton::tickDisabledColourId, t.textDim);
    setColour (juce::ColourSelector::backgroundColourId, t.window);
    setColour (juce::ColourSelector::labelTextColourId, t.text);
    setColour (juce::TextEditor::backgroundColourId, t.control);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.outline);
}

void SmartscopeLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (theme->window);
    g.fillPath (path);
    g.setColour (theme->outline.brighter (0.2f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void SmartscopeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                  bool isHighlighted, bool isDown)
{
    const auto& t = *theme;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = t.control;
    const bool dark = t.control.getPerceivedBrightness() < 0.5f;
    if (isDown)             fill = dark ? fill.brighter (0.25f) : fill.darker (0.08f);
    else if (isHighlighted) fill = dark ? fill.brighter (0.12f) : fill.darker (0.04f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (b.getToggleState() ? t.accent.withAlpha (0.85f) : t.outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void SmartscopeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                  : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
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
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, s);
        return;
    }

    const auto& t = *theme;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (0.5f);
    g.setColour (t.control);
    g.fillRoundedRectangle (r, 3.0f);

    auto filled = r.withRight (juce::jlimit (r.getX(), r.getRight(), sliderPos));
    g.setColour (t.accent.withAlpha (0.22f));
    g.fillRoundedRectangle (filled, 3.0f);

    g.setColour (t.outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void SmartscopeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                          juce::ComboBox&)
{
    const auto& t = *theme;
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (t.control);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (t.outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void SmartscopeLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (box.getLocalBounds().reduced (4, 1));
    label.setFont (getComboBoxFont (box));
}

juce::Font SmartscopeLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (13.0f);
}

juce::Label* SmartscopeLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (13.0f));
    l->setColour (juce::Label::textColourId, theme->text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

juce::TextLayout SmartscopeLookAndFeel::layoutTooltip (const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    s.append (text, juce::FontOptions (13.0f), theme->text);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 360.0f);
    return tl;
}

juce::Rectangle<int> SmartscopeLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                             juce::Rectangle<int> parentArea)
{
    const auto tl = layoutTooltip (tipText);
    const int w = (int) (tl.getWidth() + 20.0f);
    const int h = (int) (tl.getHeight() + 14.0f);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

void SmartscopeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (theme->window);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (theme->outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    layoutTooltip (text).draw (g, r.reduced (10.0f, 7.0f));
}

//==============================================================================
SmartscopeAudioProcessorEditor::SmartscopeAudioProcessorEditor (SmartscopeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), scope (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (scope);
    scope.onViewChanged = [this] { updateZoomText(); };

    // Header: layer chips, help, settings
    for (int layer = 0; layer < numLayers; ++layer)
    {
        chips[layer] = std::make_unique<LayerChip> (proc, layer);
        chips[layer]->onToggled = [this] { scope.repaint(); if (settingsPanel) settingsPanel->refresh(); };
        chips[layer]->updateTooltip();
        addAndMakeVisible (*chips[layer]);
    }

    helpButton.setTooltip (helpText);
    addAndMakeVisible (helpButton);

    settingsButton.setTooltip ("Settings: themes, layer colours, display options");
    settingsButton.setClickingTogglesState (false);
    settingsButton.onClick = [this] { toggleSettings(); };
    addAndMakeVisible (settingsButton);

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
    gainSlider.setTooltip ("Display gain (Ctrl + mouse wheel over the display). Does not change the audio. Double-click: 0 dB");
    addAndMakeVisible (gainSlider);
    gainAttachment = std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::gain, gainSlider);
    gainSlider.setDoubleClickReturnValue (true, 0.0);

    // Low-pass
    lowpassButton.setClickingTogglesState (true);
    lowpassButton.setTooltip ("Display-only low-pass filter, to see just the kick and bass. Does not change the audio and adds no delay.");
    addAndMakeVisible (lowpassButton);
    lowpassAttachment = std::make_unique<ButtonAttachment> (proc.apvts, ParamIDs::lowpassOn, lowpassButton);

    lowpassSlider.setSliderStyle (juce::Slider::LinearBar);
    lowpassSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
    lowpassSlider.setTooltip ("Low-pass cutoff. Double-click: 150 Hz");
    addAndMakeVisible (lowpassSlider);
    lowpassHzAttachment = std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::lowpassHz, lowpassSlider);
    lowpassSlider.setDoubleClickReturnValue (true, 150.0);

    // Hold
    holdButton.setClickingTogglesState (true);
    holdButton.setTooltip ("Keep a snapshot of the current window as a reference trace");
    holdButton.onClick = [this] { scope.setHold (holdButton.getToggleState()); };
    addAndMakeVisible (holdButton);

    zoomButton.setTooltip ("Current zoom. Click to reset (or double-click the display)");
    zoomButton.onClick = [this] { scope.resetZoom(); };
    addAndMakeVisible (zoomButton);

    setResizable (true, true);
    setResizeLimits (760, 360, 3000, 1600);
    setSize (juce::jlimit (760, 3000, proc.editorWidth), juce::jlimit (360, 1600, proc.editorHeight));

    applyTheme (proc.themeIndex.load());

    updateZoomText();
    timerCallback();
    startTimerHz (10);
}

SmartscopeAudioProcessorEditor::~SmartscopeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SmartscopeAudioProcessorEditor::applyTheme (int index)
{
    index = juce::jlimit (0, numThemes - 1, index);
    proc.themeIndex.store (index);
    lnf.setTheme (getTheme (index));

    // Rebuild slider text boxes so they pick up the new text colour
    gainSlider.lookAndFeelChanged();
    lowpassSlider.lookAndFeelChanged();
    sendLookAndFeelChange();
    if (settingsPanel != nullptr)
        settingsPanel->refresh();
    repaint();
}

void SmartscopeAudioProcessorEditor::toggleSettings()
{
    if (settingsPanel != nullptr)
    {
        settingsPanel.reset();
        settingsButton.setToggleState (false, juce::dontSendNotification);
        return;
    }

    settingsPanel = std::make_unique<SettingsPanel> (proc);
    settingsPanel->onThemeChanged = [this] (int index) { applyTheme (index); };
    settingsPanel->onSettingsChanged = [this]
    {
        scope.repaint();
        for (auto& c : chips) c->repaint();
    };
    settingsPanel->onClose = [this]
    {
        // Defer: the panel's own button is calling us
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SmartscopeAudioProcessorEditor> (this)]
        {
            if (safe != nullptr && safe->settingsPanel != nullptr)
                safe->toggleSettings();
        });
    };
    addAndMakeVisible (*settingsPanel);
    settingsPanel->sendLookAndFeelChange();   // its controls were created before they had our look-and-feel
    settingsPanel->refresh();
    settingsButton.setToggleState (true, juce::dontSendNotification);
    resized();
}

void SmartscopeAudioProcessorEditor::stepLength (int delta)
{
    const int next = juce::jlimit (0, numLengthChoices - 1, lengthBox.getSelectedItemIndex() + delta);
    lengthBox.setSelectedItemIndex (next, juce::sendNotificationSync);
}

void SmartscopeAudioProcessorEditor::updateZoomText()
{
    const double z = scope.getZoomFactor();
    zoomButton.setButtonText ("Zoom x" + (z < 10.0 ? juce::String (z, 1) : juce::String (juce::roundToInt (z))));
    zoomButton.setEnabled (z > 1.0001);
}

void SmartscopeAudioProcessorEditor::timerCallback()
{
    const double bpm = proc.getBpm();
    const double windowMs = proc.getDisplaySampleRate() > 0.0
                              ? (double) proc.getWindowSamples() / proc.getDisplaySampleRate() * 1000.0 : 0.0;

    juce::String s;
    s << (proc.isSynced() ? "SYNCED" : "FREE") << "   "
      << juce::String (bpm, 1) << " BPM   "
      << juce::String (windowMs, windowMs < 1000.0 ? 1 : 0) << " ms";
    if (proc.isClamped())
        s << "  (shortened: tempo too slow)";

    if (s != statusText || statusSynced != proc.isSynced())
    {
        statusText = s;
        statusSynced = proc.isSynced();
        repaint (statusArea);
    }

    lowpassSlider.setAlpha (lowpassButton.getToggleState() ? 1.0f : 0.55f);

    for (auto& c : chips)
    {
        c->updateTooltip();
        c->repaint();
    }
}

//==============================================================================
void SmartscopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.getThemeColours();
    g.fillAll (t.window);

    // Logo, with the maker's name underneath
    {
        auto r = headerArea.reduced (12, 0).withTrimmedTop (4);
        auto top = r.removeFromTop (19);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        const juce::String a = "Smart", b = "scope";
        juce::GlyphArrangement ga;
        ga.addLineOfText (g.getCurrentFont(), a, 0.0f, 0.0f);
        const int aw = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 1;
        g.setColour (t.text.withMultipliedBrightness (1.15f));
        g.drawText (a, top.removeFromLeft (aw), juce::Justification::centredLeft);
        g.setColour (t.accent);
        g.drawText (b, top.removeFromLeft (70), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (8.5f, juce::Font::bold).withKerningFactor (0.16f));
        g.setColour (t.textDim);
        g.drawText ("CLAES LANNG PLUGINS", r.removeFromTop (11).withWidth (130), juce::Justification::centredLeft);
    }

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (statusSynced ? t.accent : t.textDim);
    g.drawText (statusText, statusArea, juce::Justification::centredLeft);

    g.setColour (t.textDim);
    g.drawText ("Length", lengthLabelArea, juce::Justification::centredRight);
    g.drawText ("Gain", gainLabelArea, juce::Justification::centredRight);
}

void SmartscopeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (headerHeight);
    footerArea = r.removeFromBottom (footerHeight);

    scope.setBounds (r.reduced (8, 0));

    auto h = headerArea.reduced (8, 7);
    settingsButton.setBounds (h.removeFromRight (24));
    h.removeFromRight (4);
    helpButton.setBounds (h.removeFromRight (24));
    h.removeFromRight (12);
    for (int layer = numLayers - 1; layer >= 0; --layer)
    {
        chips[layer]->setBounds (h.removeFromRight (layer == 0 ? 58 : 52));
        h.removeFromRight (3);
    }
    h.removeFromLeft (128);
    statusArea = h.withTrimmedRight (8);

    if (settingsPanel != nullptr)
        settingsPanel->setBounds (getLocalBounds().withTrimmedTop (headerHeight));

    auto f = footerArea.reduced (8, 9);
    lengthLabelArea = f.removeFromLeft (44);
    f.removeFromLeft (6);
    lengthDown.setBounds (f.removeFromLeft (26));
    f.removeFromLeft (3);
    lengthBox.setBounds (f.removeFromLeft (78));
    f.removeFromLeft (3);
    lengthUp.setBounds (f.removeFromLeft (26));

    f.removeFromLeft (14);
    gainLabelArea = f.removeFromLeft (32);
    f.removeFromLeft (6);
    gainSlider.setBounds (f.removeFromLeft (104));

    f.removeFromLeft (16);
    lowpassButton.setBounds (f.removeFromLeft (76));
    f.removeFromLeft (3);
    lowpassSlider.setBounds (f.removeFromLeft (92));

    f.removeFromLeft (12);
    holdButton.setBounds (f.removeFromLeft (56));

    zoomButton.setBounds (f.removeFromRight (92));

    proc.editorWidth = getWidth();
    proc.editorHeight = getHeight();
}

//==============================================================================
void LayerChip::paintButton (juce::Graphics& g, bool isOver, bool)
{
    auto* lnf = dynamic_cast<SmartscopeLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? lnf->getThemeColours() : themes[0];
    auto ui = proc.getUISettings();

    const bool connected = proc.isLayerConnected (layer);
    const bool visible = UISettings::layerVisible (ui, layer);
    const auto colour = UISettings::layerColour (ui, layer, t);

    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isOver ? t.control.brighter (0.06f) : t.control);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (visible && connected ? colour.withAlpha (0.55f) : t.outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const float alpha = connected ? (visible ? 1.0f : 0.35f) : 0.25f;
    auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
    g.setColour (colour.withMultipliedAlpha (alpha));
    if (visible) g.fillEllipse (dot);
    else         g.drawEllipse (dot, 1.2f);

    g.setColour (t.text.withMultipliedAlpha (connected ? (visible ? 1.0f : 0.5f) : 0.4f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (getName(), r.withTrimmedLeft (19.0f), juce::Justification::centredLeft);
}

void LayerChip::clicked()
{
    auto ui = proc.getUISettings();
    ui.setProperty (UISettings::visibleKey (layer), ! UISettings::layerVisible (ui, layer), nullptr);
    repaint();
    if (onToggled)
        onToggled();
}

void LayerChip::updateTooltip()
{
    juce::String tip;
    if (layer == 0)
        tip = "Main input (this track). Click to show/hide.";
    else if (proc.isLayerConnected (layer))
        tip = juce::String (UISettings::layerLongName (layer)) + ". Click to show/hide. Colours are in Settings.";
    else
        tip = juce::String (UISettings::layerLongName (layer)) + ": nothing connected. Send a mixer track to Smartscope's "
              "sidechain input " + juce::String (layer) + " to see it here.";

    if (tip != getTooltip())
        setTooltip (tip);
}

void IconButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    auto* lnf = dynamic_cast<SmartscopeLookAndFeel*> (&getLookAndFeel());
    const auto& t = lnf != nullptr ? lnf->getThemeColours() : themes[0];

    auto r = getLocalBounds().toFloat().reduced (0.5f);
    auto fill = t.control;
    if (isDown || getToggleState()) fill = fill.interpolatedWith (t.accent, 0.15f);
    else if (isOver)                fill = fill.interpolatedWith (t.text, 0.06f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (getToggleState() ? t.accent.withAlpha (0.85f) : t.outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const auto c = r.getCentre();
    g.setColour (getToggleState() ? t.accent : t.text);

    if (icon == Icon::gear)
    {
        juce::Path gear;
        const float outerR = 7.0f, innerR = 5.2f;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i)
        {
            const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2);
            const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) (teeth * 2);
            const float rad = (i % 2 == 0) ? outerR : innerR;
            const auto p0 = c.getPointOnCircumference (rad, a0 + 0.06f);
            const auto p1 = c.getPointOnCircumference (rad, a1 - 0.06f);
            if (i == 0) gear.startNewSubPath (p0); else gear.lineTo (p0);
            gear.lineTo (p1);
        }
        gear.closeSubPath();
        gear.addEllipse (c.x - 2.4f, c.y - 2.4f, 4.8f, 4.8f);
        gear.setUsingNonZeroWinding (false);
        g.fillPath (gear);
    }
    else
    {
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("?", getLocalBounds(), juce::Justification::centred);
    }
}
