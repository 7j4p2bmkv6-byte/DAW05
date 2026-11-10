#include "Inspector.h"

using namespace ldaw;

namespace
{
    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, colours::panelLight);
        b.setWantsKeyboardFocus (false);
    }

    void styleTitle (juce::Label& l)
    {
        l.setEditable (false, true, false);
        l.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId, juce::Colours::white);
        l.setTooltip ("Double-click to rename");
    }

    void drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (colours::dimText);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (title, r.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (colours::background);
        g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
    }
}

InspectorPanel::InspectorPanel()
{
    styleTitle (trackName);
    trackName.onTextChange = [this] { if (auto* t = track()) t->setName (trackName.getText()); };
    addAndMakeVisible (trackName);

    styleTitle (clipName);
    clipName.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    clipName.onTextChange = [this] { if (auto* c = clip()) c->setName (clipName.getText()); };
    addAndMakeVisible (clipName);

    for (auto* b : { &instrumentButton, &outputButton, &mixerButton, &editClipButton, &tempoButton })
    {
        styleButton (*b);
        addAndMakeVisible (b);
    }

    instrumentButton.setTooltip ("Open the instrument panel");
    outputButton.setTooltip ("Where this track's audio goes: Master or a bus (group channel)");

    instrumentButton.onClick = [this] { if (auto* t = track()) if (onOpenInstrument) onOpenInstrument (*t); };
    mixerButton.onClick      = [this] { if (auto* t = track()) if (onShowInMixer) onShowInMixer (*t); };
    editClipButton.onClick   = [this] { if (clip() != nullptr && onEditClip) onEditClip (clipID); };
    tempoButton.onClick      = [this] { if (clip() != nullptr && onClipTempo) onClipTempo (clipID); };
    outputButton.onClick     = [this]
    {
        if (auto* t = track())
            routing::showOutputMenu (*edit, *t, &outputButton, [safe = juce::Component::SafePointer<InspectorPanel> (this)]
            {
                if (safe != nullptr) safe->refreshTexts();
            });
    };

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
    volume.setRange (-60.0, 6.0, 0.1);
    volume.setSkewFactorFromMidPoint (-12.0);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.onDragStart = [this] { if (auto* t = track()) if (auto* p = fx::paramByName (t->getVolumePlugin(), { "vol" })) p->parameterChangeGestureBegin(); };
    volume.onDragEnd   = [this] { if (auto* t = track()) if (auto* p = fx::paramByName (t->getVolumePlugin(), { "vol" })) p->parameterChangeGestureEnd(); };
    volume.onValueChange = [this] { if (auto* t = track()) if (auto* v = t->getVolumePlugin()) v->setVolumeDb ((float) volume.getValue()); };
    addAndMakeVisible (volume);

    pan.setSliderStyle (juce::Slider::LinearHorizontal);
    pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.005) return juce::String ("C");
        return juce::String (std::abs (v) * 100.0, 0) + (v < 0 ? " L" : " R");
    };
    pan.onValueChange = [this] { if (auto* t = track()) if (auto* v = t->getVolumePlugin()) v->setPan ((float) pan.getValue()); };
    addAndMakeVisible (pan);

    setTarget (nullptr, nullptr, 0, {});
}

te::AudioTrack* InspectorPanel::track() const
{
    if (edit == nullptr || ! trackID.isValid()) return nullptr;
    for (auto* t : te::getAudioTracks (*edit))
        if (t->itemID == trackID)
            return t;
    return nullptr;
}

te::Clip* InspectorPanel::clip() const
{
    if (edit == nullptr || ! clipID.isValid()) return nullptr;
    return te::findClipForID (*edit, clipID);
}

void InspectorPanel::setTarget (te::Edit* e, te::AudioTrack* t, int index, te::EditItemID c)
{
    edit = e;
    trackID = t != nullptr ? t->itemID : te::EditItemID();
    trackIndex = index;
    clipID = c;
    refreshTexts();
    resized();
    repaint();
}

void InspectorPanel::refreshTexts()
{
    auto* t = track();
    auto* c = clip();

    for (auto* comp : std::initializer_list<juce::Component*> { &trackName, &instrumentButton, &outputButton, &mixerButton, &volume, &pan })
        comp->setVisible (t != nullptr);

    if (t != nullptr)
    {
        if (! trackName.isBeingEdited())
            trackName.setText (t->getName(), juce::dontSendNotification);

        const bool audioTrack = isAudioTrack (*t);
        typeText = audioTrack ? "AUDIO TRACK" : "INSTRUMENT TRACK";

        juce::String inst = audioTrack ? juce::String ("(audio input)") : juce::String ("- no instrument -");
        for (auto p : t->pluginList.getPlugins())
            if (dynamic_cast<te::ExternalPlugin*> (&*p) != nullptr && p->isSynth())
                inst = p->getName();
        instrumentButton.setButtonText (inst);
        instrumentButton.setEnabled (! audioTrack);

        outputButton.setButtonText ("Output:  " + routing::outputName (*t));

        insertsText.clear();
        auto inserts = fx::getInserts (t->pluginList);
        for (int i = 0; i < inserts.size(); ++i)
            insertsText << juce::String (i + 1) << "  " << fx::displayName (*inserts[i])
                        << (inserts[i]->isEnabled() ? "" : "  (off)") << "\n";
        if (inserts.isEmpty()) insertsText = "No insert effects";

        juce::String sends;
        for (int bus = 0; bus < numSendBuses; ++bus)
        {
            const float db = fx::getSendDb (*t, bus);
            if (db > -59.9f)
                sends << "Send " << (bus + 1) << " (" << fx::returnEffectName (*edit, bus) << "):  "
                      << juce::String (db, 1) << " dB\n";
        }
        if (sends.isNotEmpty()) insertsText << "\n" << sends;

        if (auto* v = t->getVolumePlugin())
        {
            if (! volume.isMouseButtonDown()) volume.setValue (v->getVolumeDb(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())    pan.setValue (v->getPan(), juce::dontSendNotification);
        }
    }
    else
    {
        typeText = "No track selected";
        insertsText.clear();
    }

    clipName.setVisible (c != nullptr);
    const bool isMidi  = dynamic_cast<te::MidiClip*> (c) != nullptr;
    auto* wave = dynamic_cast<te::WaveAudioClip*> (c);
    editClipButton.setVisible (isMidi);
    tempoButton.setVisible (wave != nullptr);

    if (c != nullptr)
    {
        if (! clipName.isBeingEdited())
            clipName.setText (c->getName(), juce::dontSendNotification);

        const double s = beatOf (*edit, c->getPosition().getStart());
        const double e = beatOf (*edit, c->getPosition().getEnd());
        clipText = "Start:   bar " + beatToBarText (s) + "\n"
                 + "Length:  " + juce::String ((e - s) / 4.0, 2) + " bars\n";

        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
            clipText << "Notes:   " << mc->getSequence().getNotes().size() << "\n";

        if (wave != nullptr)
        {
            clipText << "File:    " << wave->getOriginalFile().getFileName() << "\n";
            const double bpm = audio::getSourceBpm (*wave);
            clipText << (wave->getAutoTempo() ? "Follows project tempo (" + juce::String (bpm, 1) + " BPM source)"
                                              : juce::String ("Original speed (no stretch)")) << "\n";
        }
    }
    else
    {
        clipText = "Click a clip to see its details.";
    }
    repaint();
}

void InspectorPanel::tick()
{
    if (! isShowing()) return;
    if (track() == nullptr && trackID.isValid()) trackID = {};
    if (clip() == nullptr && clipID.isValid())   clipID = {};
    refreshTexts();
}

void InspectorPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::background);
    g.fillRect (0, 0, 2, getHeight());

    auto* t = track();
    const auto col = t != nullptr ? trackColour (trackIndex) : colours::panelLight;

    g.setColour (col);
    g.fillRect (trackHeader);
    g.setColour (col.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff111214) : juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (typeText, trackHeader.reduced (10, 4).removeFromTop (12), juce::Justification::centredLeft);

    if (t != nullptr)
    {
        g.setColour (colours::dimText);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText ("VOL", volume.getX() - 34, volume.getY(), 30, volume.getHeight(), juce::Justification::centredLeft);
        g.drawText ("PAN", pan.getX() - 34, pan.getY(), 30, pan.getHeight(), juce::Justification::centredLeft);

        drawSection (g, effectsArea, "EFFECTS & SENDS");
        g.setColour (colours::text);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (insertsText, effectsArea.withTrimmedTop (18).reduced (2, 0), juce::Justification::topLeft, 12);
    }

    g.setColour (colours::panelLight);
    g.fillRect (clipHeader);
    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("CLIP", clipHeader.reduced (10, 4).removeFromTop (12), juce::Justification::centredLeft);

    g.setColour (colours::text);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawFittedText (clipText, clipInfoArea, juce::Justification::topLeft, 8);
}

void InspectorPanel::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (2);

    trackHeader = r.removeFromTop (46);
    trackName.setBounds (trackHeader.reduced (6, 2).withTrimmedTop (14));

    auto body = r.reduced (10, 8);
    instrumentButton.setBounds (body.removeFromTop (26));
    body.removeFromTop (6);
    outputButton.setBounds (body.removeFromTop (26));
    body.removeFromTop (10);

    auto volRow = body.removeFromTop (22);
    volume.setBounds (volRow.withTrimmedLeft (34));
    auto panRow = body.removeFromTop (22);
    pan.setBounds (panRow.withTrimmedLeft (34));
    body.removeFromTop (8);

    mixerButton.setBounds (body.removeFromTop (26));
    body.removeFromTop (10);

    effectsArea = body.removeFromTop (juce::jmin (170, juce::jmax (90, body.getHeight() / 2 - 40)));
    body.removeFromTop (6);

    clipHeader = juce::Rectangle<int> (r.getX(), body.getY(), r.getWidth(), 46);
    clipName.setBounds (clipHeader.reduced (6, 2).withTrimmedTop (14));
    body = body.withTrimmedTop (52);

    clipInfoArea = body.removeFromTop (96);
    body.removeFromTop (4);
    editClipButton.setBounds (body.removeFromTop (26));
    tempoButton.setBounds (editClipButton.getBounds());
}
