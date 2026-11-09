#include "MixerView.h"

using namespace ldaw;

namespace
{
    constexpr int stripWidth  = 96;
    constexpr int masterWidth = 150;
    constexpr int sideWidth   = 58;
    constexpr int panelWidth  = 320;
    constexpr int labelBarH   = 15;

    const juce::Colour stripBg    { 0xff232428 };
    const juce::Colour sectionBg  { 0xff2a2b30 };
    const juce::Colour labelBar   { 0xff17181a };
    const juce::Colour edgeCol    { 0xff111214 };
    const juce::Colour wellCol    { 0xff0e0f11 };

    const char* sectionNames[]       = { "DYNAMICS", "EQ", "INSERTS", "SENDS" };
    const juce::uint32 sectionInk[]  = { 0xffe8a23a, 0xff7fd16b, 0xffe8c84a, 0xff4fd1c5 };

    juce::String shortName (const juce::String& s, int maxChars)
    {
        auto t = s.replace ("Reverb ", "Rev ").replace ("Delay ", "Dly ").replace (" dotted", ".");
        return t.length() > maxChars ? t.substring (0, maxChars) : t;
    }

    double sendNormToDb (double n)  { const double g = n * n * 2.0; return g <= 0.001 ? -60.0 : 20.0 * std::log10 (g); }
    double sendDbToNorm (double db) { return db <= -59.9 ? 0.0 : std::sqrt (juce::Decibels::decibelsToGain (db) / 2.0); }

    void styleSmall (juce::TextButton& b, juce::Colour on)
    {
        b.setColour (juce::TextButton::buttonColourId, colours::panelLight);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setWantsKeyboardFocus (false);
    }
}

//==============================================================================
// LevelMeter
//==============================================================================
void LevelMeter::attach (te::LevelMeterPlugin* p)
{
    if (p == meterPlugin.get()) return;
    detach();
    if (p == nullptr) return;

    meterPlugin = te::Plugin::Ptr (p);
    measurer = &p->measurer;
    measurer->addClient (client);
}

void LevelMeter::detach()
{
    if (measurer != nullptr)
        measurer->removeClient (client);
    measurer = nullptr;
    meterPlugin = nullptr;
}

void LevelMeter::tick()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        float db = -100.0f;
        if (measurer != nullptr)
            db = client.getAndClearAudioLevel (ch).dB;

        levels[ch] = db > levels[ch] ? db : juce::jmax (-100.0f, levels[ch] - 2.5f);

        if (levels[ch] >= peaks[ch]) { peaks[ch] = levels[ch]; peakHold[ch] = 30; }
        else if (--peakHold[ch] <= 0) peaks[ch] = juce::jmax (-100.0f, peaks[ch] - 1.5f);
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float scaleW = 15.0f;
    auto bars = r.withTrimmedLeft (scaleW);

    g.setColour (wellCol);
    g.fillRoundedRectangle (bars, 2.0f);

    auto toNorm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };

    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -20.0f, -30.0f, -40.0f, -60.0f })
    {
        const float y = bars.getBottom() - 3.0f - toNorm (db) * (bars.getHeight() - 6.0f);
        g.setColour (colours::dimText);
        g.drawText (juce::String ((int) std::abs (db)), juce::Rectangle<float> (r.getX(), y - 5.0f, scaleW - 2.0f, 10.0f),
                    juce::Justification::centredRight);
    }

    constexpr int numLeds = 30;
    const float inner = bars.getHeight() - 6.0f;
    const float ledH = inner / (float) numLeds;
    const float colW = (bars.getWidth() - 6.0f) / 2.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = bars.getX() + 2.0f + (float) ch * (colW + 2.0f);
        const float level = toNorm (levels[ch]);
        const float peak  = toNorm (peaks[ch]);

        for (int i = 0; i < numLeds; ++i)
        {
            const float segNorm = ((float) i + 0.5f) / (float) numLeds;
            const float dbAt = segNorm * 66.0f - 60.0f;
            auto col = dbAt > 0.0f ? juce::Colour (0xffe33a2e) : dbAt > -12.0f ? juce::Colour (0xfff2c43a)
                                                                               : juce::Colour (0xff46d15a);
            const bool lit = segNorm <= level || std::abs (segNorm - peak) < 0.5f / (float) numLeds;
            g.setColour (lit ? col : col.withAlpha (0.10f));
            const float y = bars.getBottom() - 3.0f - (float) (i + 1) * ledH;
            g.fillRect (x, y + 0.6f, colW, ledH - 1.2f);
        }
    }
}

//==============================================================================
// ParamKnob
//==============================================================================
ParamKnob::ParamKnob (juce::Colour c, const juce::String& l, double def)
    : label (l), labelColour (colours::dimText), defaultNorm (def)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0, 0.0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.setDoubleClickReturnValue (true, def);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setValue (def, juce::dontSendNotification);
    slider.addMouseListener (this, false);

    slider.textFromValueFunction = [this] (double v) -> juce::String
    {
        if (resolve)
            if (auto* p = resolve (false))
                return p->getCurrentValueAsString();
        if (textFn) return textFn (v);
        return juce::String (v, 2);
    };

    slider.onDragStart = [this] { if (resolve) if (auto* p = resolve (true)) p->parameterChangeGestureBegin(); };
    slider.onDragEnd   = [this] { if (resolve) if (auto* p = resolve (false)) p->parameterChangeGestureEnd(); };
    slider.onValueChange = [this]
    {
        if (resolve)
        {
            if (auto* p = resolve (true))
            {
                p->setNormalisedParameter ((float) slider.getValue(), juce::sendNotification);
                return;
            }
        }
        if (setter) setter (slider.getValue());
    };
    addAndMakeVisible (slider);
}

ParamKnob::~ParamKnob()
{
    slider.removeMouseListener (this);
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick)
        onRightClick();
}

void ParamKnob::setLabel (const juce::String& l, juce::Colour c)
{
    if (l != label || c != labelColour) { label = l; labelColour = c; repaint(); }
}

void ParamKnob::tick()
{
    if (slider.isMouseButtonDown()) return;

    double v = defaultNorm;
    bool live = false;

    if (resolve)
        if (auto* p = resolve (false))
        {
            v = p->getCurrentNormalisedValue();
            live = true;
        }

    if (! live && getter)
    {
        v = getter();
        live = true;
    }

    slider.setValue (v, juce::dontSendNotification);
    slider.setAlpha (live ? 1.0f : 0.6f);
}

void ParamKnob::paint (juce::Graphics& g)
{
    g.setColour (labelColour);
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    g.drawText (label, getLocalBounds().removeFromBottom (11), juce::Justification::centred, true);
}

void ParamKnob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (11));
}

//==============================================================================
// MixerStrip
//==============================================================================
int MixerStrip::sectionHeight (int s)
{
    switch (s)
    {
        case secDyn:     return labelBarH + 52;
        case secEq:      return labelBarH + 4 * 42;
        case secInserts: return labelBarH + 4 * 19 + 24;
        case secSends:   return labelBarH + 2 * 50;
        default:         return 0;
    }
}

bool MixerStrip::hasSection (int s) const
{
    switch (s)
    {
        case secDyn:     return kind != Kind::fxReturn;
        case secEq:      return kind == Kind::music;
        case secInserts: return true;
        case secSends:   return kind == Kind::music;
        default:         return false;
    }
}

MixerStrip::MixerStrip (te::Edit& e, te::AudioTrack* t, Kind k, int i, juce::Colour c)
    : track (t), kind (k), edit (e), index (i), colour (c)
{
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    nameLabel.setColour (juce::Label::textColourId, colour.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff111214) : juce::Colours::white);
    nameLabel.setTooltip ("Click to show this channel in the FX rack. Double-click to rename.");
    if (kind == Kind::music)
    {
        nameLabel.setEditable (false, true, false);
        nameLabel.onTextChange = [this] { if (track) track->setName (nameLabel.getText()); };
    }
    nameLabel.addMouseListener (this, false);
    addAndMakeVisible (nameLabel);

    if (kind == Kind::music)
    {
        styleSmall (instrumentButton, colours::accent);
        instrumentButton.setTooltip ("Open the instrument panel");
        instrumentButton.onClick = [this]
        {
            if (track == nullptr) return;
            for (auto p : track->pluginList.getPlugins())
                if (dynamic_cast<te::ExternalPlugin*> (&*p) != nullptr && p->isSynth())
                { fx::openWindow (*p); return; }
        };
        addAndMakeVisible (instrumentButton);
    }

    // ---- dynamics ----------------------------------------------------------
    if (hasSection (secDyn))
    {
        styleSmall (dynOn, juce::Colour (0xfff0a53a));
        dynOn.setTooltip ("Channel compressor on/off");
        dynOn.onClick = [this] { toggleChannelPlugin ("dyn"); };
        addAndMakeVisible (dynOn);

        auto addDyn = [this] (const juce::String& label, juce::StringArray keys, double def)
        {
            auto* knob = dynKnobs.add (new ParamKnob (juce::Colour (0xff46b04f), label, def));
            knob->resolve = [this, keys] (bool create) -> te::AutomatableParameter*
            {
                auto* p = create ? fx::ensureChannelPlugin (edit, chain(), "dyn") : fx::findChannelPlugin (chain(), "dyn");
                return fx::paramByName (p, keys);
            };
            addAndMakeVisible (knob);
        };

        addDyn ("THRES", { "thresh" }, 1.0);
        addDyn ("RATIO", { "ratio" }, 0.2);
        if (kind == Kind::master)
        {
            addDyn ("ATTACK",  { "attack" }, 0.2);
            addDyn ("RELEASE", { "release" }, 0.3);
        }
    }

    // ---- EQ -------------------------------------------------------------------
    if (hasSection (secEq))
    {
        styleSmall (eqOn, juce::Colour (0xff7fd16b));
        eqOn.setTooltip ("Channel EQ on/off");
        eqOn.onClick = [this] { toggleChannelPlugin ("eq"); };
        addAndMakeVisible (eqOn);

        struct Band { const char* name; juce::uint32 col; int band; };
        const Band bands[] = { { "HF", 0xffd8473c, 3 }, { "HMF", 0xff46b04f, 2 }, { "LMF", 0xff4a78d8, 1 }, { "LF", 0xff8c9099, 0 } };

        for (auto& b : bands)
        {
            for (int f = 0; f < 2; ++f)
            {
                const bool isFreq = f == 1;
                auto* knob = eqKnobs.add (new ParamKnob (juce::Colour (b.col), isFreq ? juce::String ("FREQ") : juce::String (b.name), 0.5));
                const int band = b.band;
                knob->resolve = [this, band, isFreq] (bool create) -> te::AutomatableParameter*
                {
                    auto* p = create ? fx::ensureChannelPlugin (edit, chain(), "eq") : fx::findChannelPlugin (chain(), "eq");
                    return fx::eqParam (p, band, isFreq);
                };
                addAndMakeVisible (knob);
            }
        }
    }

    // ---- inserts ----------------------------------------------------------------
    styleSmall (addFxButton, colours::accent);
    addFxButton.setTooltip ("Add an effect to this channel (edit it in the FX rack on the right)");
    addFxButton.onClick = [this]
    {
        fx::showAddEffectMenu (edit, chain(), &addFxButton, [safe = juce::Component::SafePointer<MixerStrip> (this)]
        {
            if (safe == nullptr) return;
            safe->rebuildInserts();
            auto inserts = fx::getInserts (safe->chain());
            if (safe->onSelect) safe->onSelect (*safe, inserts.isEmpty() ? nullptr : inserts.getLast());
        });
    };
    addAndMakeVisible (addFxButton);

    // ---- sends ----------------------------------------------------------------
    if (hasSection (secSends))
    {
        for (int bus = 0; bus < numSendBuses; ++bus)
        {
            auto* knob = sendKnobs.add (new ParamKnob (fx::sendColour (bus), juce::String (bus + 1), 0.0));
            knob->getter = [this, bus] { return track != nullptr ? sendDbToNorm (fx::getSendDb (*track, bus)) : 0.0; };
            knob->setter = [this, bus] (double v) { if (track != nullptr) fx::setSendDb (edit, *track, bus, (float) sendNormToDb (v)); };
            knob->textFn = [] (double v) { const double db = sendNormToDb (v); return db <= -59.9 ? juce::String ("-inf") : juce::String (db, 1) + " dB"; };
            knob->onRightClick = [this, bus] { showSendMenu (bus); };
            knob->slider.setTooltip ("Send " + juce::String (bus + 1) + ". Right-click to choose the send effect.");
            addAndMakeVisible (knob);
        }
    }

    // ---- pan / fader / mute / solo ---------------------------------------------
    pan.resolve = [this] (bool) -> te::AutomatableParameter* { return fx::paramByName (volume(), { "pan" }); };
    pan.getter  = [this] { auto* v = volume(); return v != nullptr ? (v->getPan() + 1.0) * 0.5 : 0.5; };
    pan.setter  = [this] (double n) { if (auto* v = volume()) v->setPan ((float) (n * 2.0 - 1.0)); };
    addAndMakeVisible (pan);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    fader.setRange (-60.0, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setTextValueSuffix (" dB");
    fader.setColour (juce::Slider::thumbColourId, colour);
    fader.onDragStart = [this] { if (auto* p = fx::paramByName (volume(), { "vol" })) p->parameterChangeGestureBegin(); };
    fader.onDragEnd   = [this] { if (auto* p = fx::paramByName (volume(), { "vol" })) p->parameterChangeGestureEnd(); };
    fader.onValueChange = [this] { if (auto* v = volume()) v->setVolumeDb ((float) fader.getValue()); };
    addAndMakeVisible (fader);

    if (kind != Kind::master)
    {
        styleSmall (muteButton, juce::Colour (0xffd9a400));
        styleSmall (soloButton, juce::Colour (0xff2f9be0));
        muteButton.setClickingTogglesState (true);
        soloButton.setClickingTogglesState (true);
        muteButton.onClick = [this] { if (track) track->setMute (muteButton.getToggleState()); };
        soloButton.onClick = [this] { if (track) track->setSolo (soloButton.getToggleState()); };
        addAndMakeVisible (muteButton);
        addAndMakeVisible (soloButton);
    }

    addAndMakeVisible (meter);
    if (kind == Kind::master)      meter.attach (fx::ensureMasterMeter (edit));
    else if (track != nullptr)     meter.attach (track->getLevelMeterPlugin());

    rebuildInserts();
    tick();
}

te::PluginList& MixerStrip::chain()
{
    return track != nullptr ? track->pluginList : edit.getMasterPluginList();
}

juce::String MixerStrip::channelName() const
{
    if (kind == Kind::master) return "Master";
    return track != nullptr ? track->getName() : juce::String();
}

te::VolumeAndPanPlugin* MixerStrip::volume()
{
    if (kind == Kind::master)
        return edit.getMasterVolumePlugin().get();
    return track != nullptr ? track->getVolumePlugin() : nullptr;
}

void MixerStrip::toggleChannelPlugin (const juce::String& which)
{
    edit.getUndoManager().beginNewTransaction();
    if (auto* p = fx::findChannelPlugin (chain(), which))
        p->setEnabled (! p->isEnabled());
    else if (auto* np = fx::ensureChannelPlugin (edit, chain(), which))
        np->setEnabled (true);
}

juce::String MixerStrip::makeSignature()
{
    juce::String s;
    for (auto* p : fx::getInserts (chain()))
        s << p->itemID.toString() << fx::displayName (*p) << (p->isEnabled() ? "1" : "0") << "|";
    return s;
}

void MixerStrip::rebuildInserts()
{
    insertButtons.clear();
    auto inserts = fx::getInserts (chain());
    const int shown = juce::jmin (4, inserts.size());

    for (int i = 0; i < shown; ++i)
    {
        auto* p = inserts[i];
        const bool more = (i == 3 && inserts.size() > 4);
        auto* b = insertButtons.add (new SlotButton (more ? "+" + juce::String (inserts.size() - 3) + " more"
                                                          : juce::String (i + 1) + "  " + fx::displayName (*p)));
        styleSmall (*b, juce::Colour (0xff6b5a1e));
        b->setToggleState (p->isEnabled() && ! more, juce::dontSendNotification);
        b->setTooltip ("Click = edit in the FX rack, right-click = open / bypass / remove");

        te::Plugin::Ptr ptr (p);
        b->onClick      = [this, ptr] { if (onSelect) onSelect (*this, ptr.get()); };
        b->onRightClick = [this, ptr, b] { showInsertMenu (ptr.get(), b); };
        addChildComponent (b);
    }

    addFxButton.setEnabled (inserts.size() < fx::maxInserts);
    insertSignature = makeSignature();
    resized();
}

void MixerStrip::showInsertMenu (te::Plugin* p, juce::Component* target)
{
    if (p == nullptr) return;

    juce::PopupMenu m;
    m.addItem (1, "Edit in FX rack");
    m.addItem (4, "Open in window");
    m.addItem (2, p->isEnabled() ? "Bypass" : "Enable");
    m.addSeparator();
    m.addItem (3, "Remove");

    te::Plugin::Ptr ptr (p);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [safe = juce::Component::SafePointer<MixerStrip> (this), ptr] (int r)
    {
        if (safe == nullptr || ptr == nullptr) return;
        if (r == 1 && safe->onSelect) safe->onSelect (*safe, ptr.get());
        if (r == 4) fx::openWindow (*ptr);
        if (r == 2) ptr->setEnabled (! ptr->isEnabled());
        if (r == 3)
        {
            safe->edit.getUndoManager().beginNewTransaction();
            ptr->deleteFromParent();
        }
        safe->rebuildInserts();
    });
}

void MixerStrip::showSendMenu (int bus)
{
    juce::PopupMenu m;
    const auto current = fx::returnEffectName (edit, bus);
    m.addSectionHeader ("Send " + juce::String (bus + 1) + ":  " + current);
    for (int i = 0; i < fx::numSendPresets(); ++i)
        m.addItem (100 + i, fx::sendPresetName (i), true, fx::sendPresetName (i) == current);
    m.addSeparator();
    m.addItem (1, "Send level to 0 dB");
    m.addItem (2, "Turn send off");

    m.showMenuAsync (juce::PopupMenu::Options(),
                     [safe = juce::Component::SafePointer<MixerStrip> (this), bus] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 100)                        fx::setReturnEffect (safe->edit, bus, r - 100);
        else if (r == 1 && safe->track)      fx::setSendDb (safe->edit, *safe->track, bus, 0.0f);
        else if (r == 2 && safe->track)      fx::setSendDb (safe->edit, *safe->track, bus, -60.0f);
    });
}

void MixerStrip::setSections (const bool visible[numSections])
{
    for (int s = 0; s < numSections; ++s)
        show[s] = visible[s];
    resized();
    repaint();
}

void MixerStrip::setSelected (bool s)
{
    if (s != selected) { selected = s; repaint(); }
}

void MixerStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (onSelect) onSelect (*this, nullptr);
}

void MixerStrip::tick()
{
    if (kind == Kind::master)
        nameLabel.setText ("MASTER", juce::dontSendNotification);
    else if (track != nullptr && ! nameLabel.isBeingEdited())
        nameLabel.setText (track->getName(), juce::dontSendNotification);

    if (kind == Kind::music && track != nullptr)
    {
        juce::String inst = isAudioTrack (*track) ? "AUDIO" : "- no instrument -";
        for (auto p : track->pluginList.getPlugins())
            if (dynamic_cast<te::ExternalPlugin*> (&*p) != nullptr && p->isSynth())
                inst = p->getName();
        instrumentButton.setButtonText (inst);
    }

    if (auto* dyn = fx::findChannelPlugin (chain(), "dyn")) dynOn.setToggleState (dyn->isEnabled(), juce::dontSendNotification);
    else                                                     dynOn.setToggleState (false, juce::dontSendNotification);
    if (auto* eq = fx::findChannelPlugin (chain(), "eq"))    eqOn.setToggleState (eq->isEnabled(), juce::dontSendNotification);
    else                                                     eqOn.setToggleState (false, juce::dontSendNotification);

    for (auto* k : dynKnobs)  k->tick();
    for (auto* k : eqKnobs)   k->tick();
    for (int bus = 0; bus < sendKnobs.size(); ++bus)
    {
        sendKnobs[bus]->tick();
        sendKnobs[bus]->setLabel (juce::String (bus + 1) + " " + shortName (fx::returnEffectName (edit, bus), 7), fx::sendColour (bus));
    }
    pan.tick();

    if (auto* v = volume())
        if (! fader.isMouseButtonDown())
            fader.setValue (v->getVolumeDb(), juce::dontSendNotification);

    if (track != nullptr && kind != Kind::master)
    {
        muteButton.setToggleState (track->isMuted (false), juce::dontSendNotification);
        soloButton.setToggleState (track->isSolo (false), juce::dontSendNotification);
    }

    if (makeSignature() != insertSignature)
        rebuildInserts();

    meter.tick();
}

void MixerStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f, 0.0f);

    g.setColour (stripBg);
    g.fillRect (r);
    g.setColour (edgeCol);
    g.drawRect (r, 1.0f);

    g.setColour (colour);
    g.fillRect (headerArea.toFloat().reduced (2.0f, 2.0f));

    for (int s = 0; s < numSections; ++s)
    {
        if (! show[s]) continue;
        auto area = sectionArea[s];
        auto bar = area.removeFromTop (labelBarH);

        g.setColour (labelBar);
        g.fillRect (bar.reduced (1, 1));
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (bar.reduced (1, 1).removeFromLeft (3));

        if (! hasSection (s))
        {
            g.setColour (stripBg.darker (0.15f));
            g.fillRect (area.reduced (2, 1));
            continue;
        }

        g.setColour (juce::Colour (sectionInk[s]));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        auto text = (kind == Kind::master && s == secDyn) ? juce::String ("MASTER COMP") : juce::String (sectionNames[s]);
        g.drawText (text, bar.withTrimmedLeft (8), juce::Justification::centredLeft, false);

        g.setColour (sectionBg);
        g.fillRect (area.reduced (2, 1));
    }

    g.setColour (sectionBg.darker (0.08f));
    g.fillRect (bottomArea.reduced (2, 1));

    if (kind == Kind::music)
    {
        g.setColour (colours::dimText);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (juce::String (index + 1), bottomArea.getX() + 4, bottomArea.getBottom() - 16, 20, 14, juce::Justification::left);
    }

    if (selected)
    {
        g.setColour (colours::accent);
        g.drawRect (getLocalBounds().toFloat().reduced (1.0f, 0.5f), 2.0f);
    }
}

void MixerStrip::resized()
{
    const int w = getWidth();
    int y = 0;

    headerArea = { 0, y, w, headerHeight };
    nameLabel.setBounds (headerArea.reduced (4, 3));
    y += headerHeight;

    for (int s = 0; s < numSections; ++s)
    {
        sectionArea[s] = show[s] ? juce::Rectangle<int> (0, y, w, sectionHeight (s)) : juce::Rectangle<int>();
        if (show[s]) y += sectionHeight (s);
    }
    bottomArea = { 0, y, w, juce::jmax (0, getHeight() - y) };

    const bool dynVis = show[secDyn] && hasSection (secDyn);
    dynOn.setVisible (dynVis);
    for (auto* k : dynKnobs) k->setVisible (dynVis);
    if (dynVis)
    {
        auto a = sectionArea[secDyn];
        auto bar = a.removeFromTop (labelBarH);
        dynOn.setBounds (bar.removeFromRight (30).reduced (2, 2));
        a = a.reduced (4, 2);
        const int kw = a.getWidth() / juce::jmax (1, dynKnobs.size());
        for (auto* k : dynKnobs)
            k->setBounds (a.removeFromLeft (kw));
    }

    const bool eqVis = show[secEq] && hasSection (secEq);
    eqOn.setVisible (eqVis);
    for (auto* k : eqKnobs) k->setVisible (eqVis);
    if (eqVis)
    {
        auto a = sectionArea[secEq];
        auto bar = a.removeFromTop (labelBarH);
        eqOn.setBounds (bar.removeFromRight (30).reduced (2, 2));
        a = a.reduced (4, 1);
        for (int row = 0; row < 4; ++row)
        {
            auto line = a.removeFromTop (42);
            if (row * 2 + 1 < eqKnobs.size())
            {
                eqKnobs[row * 2]->setBounds (line.removeFromLeft (line.getWidth() / 2));
                eqKnobs[row * 2 + 1]->setBounds (line);
            }
        }
    }

    const bool insVis = show[secInserts];
    addFxButton.setVisible (insVis);
    for (auto* b : insertButtons) b->setVisible (insVis);
    if (insVis)
    {
        auto a = sectionArea[secInserts].withTrimmedTop (labelBarH).reduced (4, 3);
        for (int i = 0; i < 4; ++i)
        {
            auto slot = a.removeFromTop (19).reduced (0, 1);
            if (i < insertButtons.size()) insertButtons[i]->setBounds (slot);
        }
        addFxButton.setBounds (a.removeFromTop (22).reduced (6, 2));
    }

    const bool sendVis = show[secSends] && hasSection (secSends);
    for (auto* k : sendKnobs) k->setVisible (sendVis);
    if (sendVis)
    {
        auto a = sectionArea[secSends].withTrimmedTop (labelBarH).reduced (2, 1);
        const int cw = a.getWidth() / 2;
        for (int i = 0; i < sendKnobs.size(); ++i)
            sendKnobs[i]->setBounds (a.getX() + (i % 2) * cw, a.getY() + (i / 2) * 50, cw, 50);
    }

    auto b = bottomArea.reduced (4, 4);
    if (kind == Kind::music)
        instrumentButton.setBounds (b.removeFromTop (20));
    b.removeFromTop (2);
    pan.setBounds (b.removeFromTop (44).withSizeKeepingCentre (48, 44));

    if (kind != Kind::master)
    {
        auto ms = b.removeFromBottom (22);
        b.removeFromBottom (14);
        muteButton.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 0));
        soloButton.setBounds (ms.reduced (1, 0));
    }

    b.removeFromBottom (2);
    const int meterW = kind == Kind::master ? 44 : 36;
    meter.setBounds (b.removeFromRight (meterW).withTrimmedBottom (18));
    fader.setBounds (b);
}

//==============================================================================
// FxRackPanel
//==============================================================================
struct FxRackPanel::Row : public juce::Component
{
    Row()
    {
        power.setClickingTogglesState (true);
        styleSmall (power, juce::Colour (0xff3fbf6a));
        styleSmall (name, colours::accent);
        for (auto* b : { &up, &down, &remove })
            styleSmall (*b, colours::accent);
        power.setTooltip ("On / bypass");
        name.setTooltip ("Edit this effect below");
        up.setTooltip ("Move up (earlier in the chain)");
        down.setTooltip ("Move down (later in the chain)");
        remove.setTooltip ("Remove");
        for (auto* b : { &power, &name, &up, &down, &remove })
            addAndMakeVisible (b);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (isSelected ? colours::panelLight.brighter (0.08f) : colours::panel);
        g.fillRoundedRectangle (r, 3.0f);
        if (isSelected)
        {
            g.setColour (colours::accent);
            g.drawRoundedRectangle (r, 3.0f, 1.5f);
        }
        g.setColour (colours::dimText);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (number), 4, 0, 18, getHeight(), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (3, 3);
        r.removeFromLeft (20);
        power.setBounds (r.removeFromLeft (38));
        r.removeFromLeft (3);
        remove.setBounds (r.removeFromRight (24));
        down.setBounds (r.removeFromRight (24));
        up.setBounds (r.removeFromRight (24));
        r.removeFromRight (3);
        name.setBounds (r);
    }

    int number = 0;
    bool isSelected = false;
    juce::TextButton power, name, up { "^" }, down { "v" }, remove { "x" };
};

FxRackPanel::FxRackPanel()
{
    rowsView.setViewedComponent (&rowsContent, false);
    rowsView.setScrollBarsShown (true, false);
    addAndMakeVisible (rowsView);

    editorView.setScrollBarsShown (true, false);
    addAndMakeVisible (editorView);

    styleSmall (addButton, colours::accent);
    styleSmall (windowButton, colours::accent);
    addButton.setTooltip ("Add an effect at the end of this channel's chain");
    windowButton.setTooltip ("Open the selected effect in its own window");

    addButton.onClick = [this]
    {
        if (edit == nullptr || list == nullptr) return;
        fx::showAddEffectMenu (*edit, *list, &addButton, [safe = juce::Component::SafePointer<FxRackPanel> (this)]
        {
            if (safe == nullptr || safe->list == nullptr) return;
            auto inserts = fx::getInserts (*safe->list);
            safe->selected = inserts.isEmpty() ? nullptr : inserts.getLast();
            safe->rebuild();
        });
    };
    windowButton.onClick = [this] { if (selected != nullptr) fx::openWindow (*selected); };

    editorTitle.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    editorTitle.setColour (juce::Label::textColourId, colours::text);

    addAndMakeVisible (addButton);
    addAndMakeVisible (windowButton);
    addAndMakeVisible (editorTitle);
}

FxRackPanel::~FxRackPanel()
{
    editorView.setViewedComponent (nullptr, false);
    editor.reset();
    rows.clear();
}

void FxRackPanel::clear()
{
    edit = nullptr;
    list = nullptr;
    selected = nullptr;
    rebuild();
}

void FxRackPanel::setChannel (te::Edit* e, te::PluginList* l, const juce::String& name, juce::Colour c)
{
    const bool changed = l != list;
    edit = e;
    list = l;
    title = name;
    colour = c;
    if (changed) selected = nullptr;
    rebuild();
    repaint();
}

void FxRackPanel::selectPlugin (te::Plugin* p)
{
    selected = p;
    rebuild();
}

juce::String FxRackPanel::makeSignature() const
{
    juce::String s;
    if (list != nullptr)
        for (auto* p : fx::getInserts (*list))
            s << p->itemID.toString() << fx::displayName (*p) << (p->isEnabled() ? "1" : "0") << "|";
    return s;
}

void FxRackPanel::rebuild()
{
    rows.clear();
    juce::Array<te::Plugin*> inserts;
    if (list != nullptr) inserts = fx::getInserts (*list);

    if (selected != nullptr && ! inserts.contains (selected.get()))
        selected = nullptr;
    if (selected == nullptr && ! inserts.isEmpty())
        selected = inserts.getFirst();

    for (int i = 0; i < inserts.size(); ++i)
    {
        te::Plugin::Ptr p (inserts[i]);
        auto* row = rows.add (new Row());
        row->number = i + 1;
        row->isSelected = p == selected;
        row->power.setToggleState (p->isEnabled(), juce::dontSendNotification);
        row->power.setButtonText (p->isEnabled() ? "ON" : "OFF");
        row->name.setButtonText (fx::displayName (*p));
        row->up.setEnabled (i > 0);
        row->down.setEnabled (i < inserts.size() - 1);

        juce::Component::SafePointer<FxRackPanel> safe (this);
        row->power.onClick  = [safe, p] { if (safe == nullptr) return; p->setEnabled (! p->isEnabled()); safe->rebuildAsync(); };
        row->name.onClick   = [safe, p] { if (safe == nullptr) return; safe->selected = p; safe->rebuildAsync(); };
        row->up.onClick     = [safe, p] { if (safe == nullptr || safe->list == nullptr) return; fx::movePlugin (*safe->list, *p, -1); safe->rebuildAsync(); };
        row->down.onClick   = [safe, p] { if (safe == nullptr || safe->list == nullptr) return; fx::movePlugin (*safe->list, *p,  1); safe->rebuildAsync(); };
        row->remove.onClick = [safe, p]
        {
            if (safe == nullptr || safe->edit == nullptr) return;
            safe->edit->getUndoManager().beginNewTransaction();
            if (safe->selected == p) safe->selected = nullptr;
            p->deleteFromParent();
            safe->rebuildAsync();
        };
        rowsContent.addAndMakeVisible (row);
    }

    addButton.setEnabled (list != nullptr && inserts.size() < fx::maxInserts);
    signature = makeSignature();
    showEditorFor (selected.get());
    resized();
}

void FxRackPanel::rebuildAsync()
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FxRackPanel> (this)]
    {
        if (safe != nullptr) safe->rebuild();
    });
}

void FxRackPanel::showEditorFor (te::Plugin* p)
{
    if (p != nullptr && p == editorPlugin.get() && editor != nullptr) return;

    editorView.setViewedComponent (nullptr, false);
    editor.reset();
    editorPlugin = p;

    if (p == nullptr)
    {
        editorTitle.setText (list == nullptr ? "Click a channel to see its effects"
                                             : "No effects yet - click + Add effect", juce::dontSendNotification);
        windowButton.setEnabled (false);
        return;
    }

    editorTitle.setText (fx::displayName (*p), juce::dontSendNotification);
    windowButton.setEnabled (true);

    if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
    {
        auto* l = new juce::Label ({}, "VST3 effect - use 'Open window' to see its own panel.");
        l->setColour (juce::Label::textColourId, colours::dimText);
        l->setJustificationType (juce::Justification::centred);
        l->setSize (300, 60);
        editor.reset (l);
    }
    else
    {
        editor = std::make_unique<GenericPluginEditor> (*p);
    }

    editorView.setViewedComponent (editor.get(), false);
    resized();
}

void FxRackPanel::tick()
{
    if (makeSignature() != signature)
        rebuild();
}

void FxRackPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel.darker (0.15f));

    auto r = getLocalBounds();
    auto header = r.removeFromTop (28);
    g.setColour (colours::background);
    g.fillRect (header);
    g.setColour (list != nullptr ? colour : colours::dimText);
    g.fillRect (header.removeFromLeft (5));
    g.setColour (colours::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (list != nullptr ? "FX RACK  -  " + title : juce::String ("FX RACK"),
                header.reduced (8, 0), juce::Justification::centredLeft, true);

    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("Signal flows top to bottom", getLocalBounds().removeFromTop (28).reduced (8, 0),
                juce::Justification::centredRight, true);
}

void FxRackPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (28);
    r.reduce (6, 4);

    const int rowH = 28;
    const int wantRows = juce::jmax (1, rows.size()) * rowH;
    const int rowsH = juce::jmin (wantRows, juce::jmax (rowH, (r.getHeight() - 60) / 2));
    rowsView.setBounds (r.removeFromTop (rowsH));
    const int cw = rowsView.getWidth() - (wantRows > rowsH ? rowsView.getScrollBarThickness() : 0);
    rowsContent.setSize (juce::jmax (100, cw), rows.size() * rowH);
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, rowsContent.getWidth(), rowH);

    r.removeFromTop (4);
    addButton.setBounds (r.removeFromTop (26).removeFromLeft (130));
    r.removeFromTop (8);

    auto titleRow = r.removeFromTop (24);
    windowButton.setBounds (titleRow.removeFromRight (100).reduced (0, 1));
    editorTitle.setBounds (titleRow);
    r.removeFromTop (2);

    editorView.setBounds (r);
    if (editor != nullptr)
        editor->setSize (juce::jmax (200, editorView.getMaximumVisibleWidth()), editor->getHeight());
}

//==============================================================================
// SendsPanel
//==============================================================================
struct SendsPanel::Row
{
    juce::ComboBox effect;
    juce::Slider level;
    juce::TextButton editButton { "Edit" };
};

SendsPanel::SendsPanel()
{
    for (int bus = 0; bus < numSendBuses; ++bus)
    {
        auto* row = rows.add (new Row());

        for (int i = 0; i < fx::numSendPresets(); ++i)
            row->effect.addItem (fx::sendPresetName (i), i + 1);
        row->effect.setTooltip ("What send " + juce::String (bus + 1) + " does - used by every channel's send knob " + juce::String (bus + 1));
        row->effect.onChange = [this, bus, row]
        {
            const int id = row->effect.getSelectedId();
            if (edit != nullptr && id > 0)
                fx::setReturnEffect (*edit, bus, id - 1);
        };

        row->level.setSliderStyle (juce::Slider::LinearHorizontal);
        row->level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        row->level.setRange (-60.0, 6.0, 0.1);
        row->level.setSkewFactorFromMidPoint (-12.0);
        row->level.setDoubleClickReturnValue (true, 0.0);
        row->level.setColour (juce::Slider::thumbColourId, fx::sendColour (bus));
        row->level.setColour (juce::Slider::trackColourId, fx::sendColour (bus).withAlpha (0.6f));
        row->level.setTooltip ("Return level of send " + juce::String (bus + 1));
        row->level.onValueChange = [this, bus, row]
        {
            if (edit == nullptr) return;
            if (auto* t = fx::returnTrack (*edit, bus))
                if (auto* v = t->getVolumePlugin())
                    v->setVolumeDb ((float) row->level.getValue());
        };

        styleSmall (row->editButton, colours::accent);
        row->editButton.setTooltip ("Edit this send effect in the FX rack");
        row->editButton.onClick = [this, bus] { if (onEdit) onEdit (bus); };

        addAndMakeVisible (row->effect);
        addAndMakeVisible (row->level);
        addAndMakeVisible (row->editButton);
    }
}

SendsPanel::~SendsPanel()
{
    rows.clear();
}

void SendsPanel::setEdit (te::Edit* e)
{
    edit = e;
    tick();
}

void SendsPanel::tick()
{
    if (edit == nullptr) return;

    for (int bus = 0; bus < rows.size(); ++bus)
    {
        auto* row = rows[bus];
        const auto name = fx::returnEffectName (*edit, bus);

        if (! row->effect.isPopupActive())
        {
            int id = 0;
            for (int i = 0; i < fx::numSendPresets(); ++i)
                if (fx::sendPresetName (i) == name) id = i + 1;

            if (id > 0) row->effect.setSelectedId (id, juce::dontSendNotification);
            else        row->effect.setText (name, juce::dontSendNotification);
        }

        if (auto* t = fx::returnTrack (*edit, bus))
            if (auto* v = t->getVolumePlugin())
                if (! row->level.isMouseButtonDown())
                    row->level.setValue (v->getVolumeDb(), juce::dontSendNotification);
    }
}

void SendsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel.darker (0.15f));

    auto r = getLocalBounds();
    auto header = r.removeFromTop (28);
    g.setColour (colours::background);
    g.fillRect (header);
    g.setColour (colours::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText ("SEND FX", header.reduced (10, 0), juce::Justification::centredLeft);

    r.reduce (6, 6);
    for (int bus = 0; bus < rows.size(); ++bus)
    {
        auto box = r.removeFromTop (64).reduced (0, 3);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (box.toFloat(), 4.0f);
        auto badge = box.removeFromLeft (26);
        g.setColour (fx::sendColour (bus));
        g.fillRoundedRectangle (badge.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff111214));
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        g.drawText (juce::String (bus + 1), badge, juce::Justification::centred);
    }

    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawFittedText ("Each channel has send knobs 1-4. Pick the effect per send here, "
                      "or right-click a send knob in the mixer. The effect is always 100 % wet.",
                      r.reduced (4, 6).removeFromTop (60), juce::Justification::topLeft, 4);
}

void SendsPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (28);
    r.reduce (6, 6);
    for (auto* row : rows)
    {
        auto box = r.removeFromTop (64).reduced (0, 3).withTrimmedLeft (32).reduced (4, 4);
        auto top = box.removeFromTop (26);
        row->editButton.setBounds (top.removeFromRight (52));
        top.removeFromRight (4);
        row->effect.setBounds (top);
        box.removeFromTop (2);
        row->level.setBounds (box);
    }
}

//==============================================================================
// MixerView
//==============================================================================
MixerView::MixerView()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    const char* names[] = { "DYN", "EQ", "INS", "SEND" };
    for (int s = 0; s < MixerStrip::numSections; ++s)
    {
        auto& b = sectionButtons[s];
        b.setButtonText (names[s]);
        b.setClickingTogglesState (true);
        b.setToggleState (true, juce::dontSendNotification);
        styleSmall (b, juce::Colour (0xff6b5a1e));
        b.setTooltip ("Show / hide this section (sections fold away automatically when the mixer is small)");
        b.onClick = [this, s] { userShow[s] = sectionButtons[s].getToggleState(); resized(); };
        addAndMakeVisible (b);
    }

    styleSmall (panelButton, colours::accent);
    panelButton.setClickingTogglesState (true);
    panelButton.setToggleState (true, juce::dontSendNotification);
    panelButton.setTooltip ("Show / hide the FX rack and send FX panel");
    panelButton.onClick = [this] { panelShown = panelButton.getToggleState(); resized(); };
    addAndMakeVisible (panelButton);

    for (auto* b : { &rackTab, &sendsTab })
    {
        styleSmall (*b, colours::accent);
        addAndMakeVisible (b);
    }
    rackTab.onClick  = [this] { setTab (false); };
    sendsTab.onClick = [this] { setTab (true); };

    sends.onEdit = [this] (int bus) { selectBus (bus); };

    addAndMakeVisible (rack);
    addChildComponent (sends);
    setTab (false);
}

MixerView::~MixerView()
{
    rack.clear();
    strips.clear();
}

void MixerView::setTab (bool s)
{
    showSends = s;
    rackTab.setToggleState (! s, juce::dontSendNotification);
    sendsTab.setToggleState (s, juce::dontSendNotification);
    resized();
}

void MixerView::setEdit (te::Edit* e)
{
    rack.clear();
    strips.clear();
    edit = e;
    rackTrackID = {};
    sends.setEdit (e);
    rebuild();
}

juce::String MixerView::makeTrackSignature()
{
    juce::String s;
    if (edit != nullptr)
        for (auto* t : te::getAudioTracks (*edit))
            s << t->itemID.toString() << ",";
    return s;
}

void MixerView::selectStrip (MixerStrip& strip, te::Plugin* plugin)
{
    rackTrackID = strip.track != nullptr ? strip.track->itemID : te::EditItemID();
    for (auto* s : strips)
        s->setSelected (s == &strip);

    rack.setChannel (edit, &strip.chain(), strip.channelName(), strip.getColour());
    if (plugin != nullptr)
        rack.selectPlugin (plugin);

    if (showSends) setTab (false);
}

void MixerView::selectBus (int bus)
{
    for (auto* s : strips)
        if (s->kind == MixerStrip::Kind::fxReturn && s->track != nullptr && returnBusOf (*s->track) == bus)
        {
            selectStrip (*s, nullptr);
            setTab (false);
            return;
        }
}

void MixerView::restoreRackSelection()
{
    for (auto* s : strips)
    {
        const bool match = rackTrackID.isValid() ? (s->track != nullptr && s->track->itemID == rackTrackID)
                                                 : s->kind == MixerStrip::Kind::master;
        if (match) { selectStrip (*s, nullptr); return; }
    }

    rackTrackID = {};
    for (auto* s : strips)
        if (s->kind == MixerStrip::Kind::master) { selectStrip (*s, nullptr); return; }
}

void MixerView::rebuild()
{
    rack.clear();
    strips.clear();
    trackSignature = makeTrackSignature();
    if (edit == nullptr) return;

    auto addStrip = [this] (MixerStrip* s)
    {
        s->onSelect = [this] (MixerStrip& st, te::Plugin* p) { selectStrip (st, p); };
        content.addAndMakeVisible (strips.add (s));
    };

    int i = 0;
    for (auto* t : getMusicTracks (*edit))
    {
        addStrip (new MixerStrip (*edit, t, MixerStrip::Kind::music, i, trackColour (i)));
        ++i;
    }

    for (auto* t : getReturnTracks (*edit))
        addStrip (new MixerStrip (*edit, t, MixerStrip::Kind::fxReturn, i++, fx::sendColour (returnBusOf (*t))));

    addStrip (new MixerStrip (*edit, nullptr, MixerStrip::Kind::master, i++, colours::accent));

    restoreRackSelection();
    resized();
}

void MixerView::tick()
{
    if (! isShowing()) return;

    if (makeTrackSignature() != trackSignature)
        rebuild();

    for (auto* s : strips)
        s->tick();

    if (panelShown)
    {
        if (showSends) sends.tick();
        else           rack.tick();
    }
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto side = getLocalBounds().removeFromLeft (sideWidth);
    g.setColour (colours::panel);
    g.fillRect (side.reduced (2));
    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("SHOW", side.removeFromTop (22), juce::Justification::centred);
}

void MixerView::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (sideWidth).reduced (6, 4);
    side.removeFromTop (18);
    for (auto& b : sectionButtons)
        b.setBounds (side.removeFromTop (26).reduced (0, 2));
    side.removeFromTop (10);
    panelButton.setBounds (side.removeFromTop (26).reduced (0, 2));

    rack.setVisible (panelShown && ! showSends);
    sends.setVisible (panelShown && showSends);
    rackTab.setVisible (panelShown);
    sendsTab.setVisible (panelShown);
    if (panelShown)
    {
        auto panel = r.removeFromRight (juce::jmin (panelWidth, juce::jmax (220, r.getWidth() / 3)));
        auto tabs = panel.removeFromTop (26).reduced (4, 2);
        rackTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (1, 0));
        sendsTab.setBounds (tabs.reduced (1, 0));
        rack.setBounds (panel);
        sends.setBounds (panel);
        r.removeFromRight (2);
    }

    viewport.setBounds (r);

    const int minH = MixerStrip::headerHeight + MixerStrip::bottomMinHeight;
    const int avail = juce::jmax (minH, viewport.getHeight() - viewport.getScrollBarThickness());

    bool vis[MixerStrip::numSections];
    int need = minH;
    for (int s = 0; s < MixerStrip::numSections; ++s)
    {
        vis[s] = userShow[s];
        if (vis[s]) need += MixerStrip::sectionHeight (s);
    }
    for (int s = 0; s < MixerStrip::numSections && need > avail; ++s)
        if (vis[s]) { vis[s] = false; need -= MixerStrip::sectionHeight (s); }

    for (int s = 0; s < MixerStrip::numSections; ++s)
        sectionButtons[s].setAlpha (userShow[s] && ! vis[s] ? 0.45f : 1.0f);

    int x = 2, numMusic = 0;
    for (auto* s : strips)
        if (s->kind == MixerStrip::Kind::music) ++numMusic;

    for (int i = 0; i < strips.size(); ++i)
    {
        auto* s = strips[i];
        if (i == numMusic) x += 10;
        if (s->kind == MixerStrip::Kind::master) x += 10;
        const int w = s->kind == MixerStrip::Kind::master ? masterWidth : stripWidth;
        s->setSections (vis);
        s->setBounds (x, 0, w, avail);
        x += w;
    }

    content.setSize (juce::jmax (x + 2, viewport.getMaximumVisibleWidth()), avail);
}
