#include "MainComponent.h"

//==============================================================================
namespace
{
    te::ExternalPlugin* findInstrument (te::AudioTrack& t)
    {
        for (auto p : t.pluginList.getPlugins())
            if (auto* ep = dynamic_cast<te::ExternalPlugin*> (&*p))
                if (ep->isSynth())
                    return ep;
        return nullptr;
    }

    void styleToggle (juce::TextButton& b, juce::Colour onColour)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, onColour);
        b.setColour (juce::TextButton::buttonColourId, colours::panelLight);
    }

    juce::File projectFolder()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("L-DAW");
        dir.createDirectory();
        return dir;
    }

    juce::String pluginTitle (te::Plugin& p)
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (&p) != nullptr) return "Mixer";
        return fx::displayName (p);
    }

    bool isAutomatablePlugin (te::Plugin& p)
    {
        return dynamic_cast<te::LevelMeterPlugin*> (&p) == nullptr
            && dynamic_cast<te::AuxReturnPlugin*> (&p) == nullptr
            && p.getNumAutomatableParameters() > 0;
    }
}

//==============================================================================
// TrackRow
//==============================================================================
TrackRow::TrackRow (te::AudioTrack& t, int i, juce::Colour c)
    : track (t), index (i), isAudio (isAudioTrack (t)), colour (c)
{
    nameLabel.setEditable (false, true, false);
    nameLabel.setColour (juce::Label::textColourId, colours::text);
    nameLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    nameLabel.onTextChange = [this] { track.setName (nameLabel.getText()); };
    nameLabel.addMouseListener (this, false);
    addAndMakeVisible (nameLabel);

    instrumentButton.setColour (juce::TextButton::buttonColourId, colours::panelLight);
    instrumentButton.setTooltip (isAudio ? "Choose the audio input to record from" : "Choose instrument plug-in");
    instrumentButton.onClick = [this]
    {
        if (isAudio) { if (onChooseInput) onChooseInput (*this); }
        else         { if (onChooseInstrument) onChooseInstrument (*this); }
    };
    addAndMakeVisible (instrumentButton);

    editButton.setColour (juce::TextButton::buttonColourId, colours::panelLight);
    editButton.setTooltip ("Open instrument panel");
    editButton.onClick = [this] { if (onOpenInstrument) onOpenInstrument (*this); };
    if (! isAudio) addAndMakeVisible (editButton);

    autoButton.setColour (juce::TextButton::buttonColourId, colours::panelLight);
    autoButton.setColour (juce::TextButton::textColourOffId, colours::automation);
    autoButton.setTooltip ("Automation lanes for this track");
    autoButton.onClick = [this] { if (onAddAutomation) onAddAutomation (*this); };
    addAndMakeVisible (autoButton);

    styleToggle (armButton,  colours::record);
    styleToggle (muteButton, juce::Colour (0xffd9a400));
    styleToggle (soloButton, juce::Colour (0xff2f9be0));
    armButton.setTooltip (isAudio ? "Record arm (records from the chosen audio input)"
                                  : "Record arm (also routes your MIDI keyboard to this track)");
    armButton.onClick  = [this] { if (onArm) onArm (*this, armButton.getToggleState()); };
    muteButton.onClick = [this] { track.setMute (muteButton.getToggleState()); };
    soloButton.onClick = [this] { track.setSolo (soloButton.getToggleState()); };
    addAndMakeVisible (armButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setColour (juce::Slider::thumbColourId, colour);
    volumeSlider.setColour (juce::Slider::trackColourId, colour.withAlpha (0.6f));
    volumeSlider.setPopupDisplayEnabled (true, true, nullptr);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.onDragStart = [this] { if (auto* p = fx::paramByName (track.getVolumePlugin(), { "vol" })) p->parameterChangeGestureBegin(); };
    volumeSlider.onDragEnd   = [this] { if (auto* p = fx::paramByName (track.getVolumePlugin(), { "vol" })) p->parameterChangeGestureEnd(); };
    volumeSlider.onValueChange = [this]
    {
        if (auto vp = track.getVolumePlugin())
            vp->setVolumeDb ((float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    panSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    panSlider.setPopupDisplayEnabled (true, true, nullptr);
    panSlider.onDragStart = [this] { if (auto* p = fx::paramByName (track.getVolumePlugin(), { "pan" })) p->parameterChangeGestureBegin(); };
    panSlider.onDragEnd   = [this] { if (auto* p = fx::paramByName (track.getVolumePlugin(), { "pan" })) p->parameterChangeGestureEnd(); };
    panSlider.onValueChange = [this]
    {
        if (auto vp = track.getVolumePlugin())
            vp->setPan ((float) panSlider.getValue());
    };
    addAndMakeVisible (panSlider);

    refresh();
}

void TrackRow::refresh()
{
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (track.getName(), juce::dontSendNotification);

    if (isAudio)
    {
        auto in = track.state.getProperty (inputProp).toString();
        if (in.isEmpty())
        {
            auto names = audio::getWaveInputNames (track.edit.engine);
            in = names.isEmpty() ? juce::String ("no input") : names[0];
        }
        instrumentButton.setButtonText ("In: " + in);
    }
    else if (auto* ep = findInstrument (track))
    {
        instrumentButton.setButtonText (ep->getName());
    }
    else
    {
        instrumentButton.setButtonText ("- no instrument -");
    }

    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);
    soloButton.setToggleState (track.isSolo (false),  juce::dontSendNotification);

    if (auto vp = track.getVolumePlugin())
    {
        if (! volumeSlider.isMouseButtonDown()) volumeSlider.setValue (vp->getVolumeDb(), juce::dontSendNotification);
        if (! panSlider.isMouseButtonDown())    panSlider.setValue (vp->getPan(), juce::dontSendNotification);
    }
}

void TrackRow::setSelected (bool s) { selected = s; repaint(); }
void TrackRow::setArmed (bool a)    { armButton.setToggleState (a, juce::dontSendNotification); }

void TrackRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (selected ? colours::panelLight : colours::panel);
    g.fillRect (r);

    g.setColour (selected ? colour : colour.withAlpha (0.55f));
    g.fillRect (r.removeFromLeft (6));

    g.setColour (colours::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (juce::String (index + 1) + (isAudio ? "  AUDIO" : "  INST"), 12, 4, 80, 12, juce::Justification::left);
}

void TrackRow::resized()
{
    auto r = getLocalBounds().reduced (6, 6).withTrimmedLeft (6);

    auto left = r.removeFromLeft (124);
    left.removeFromTop (12);
    nameLabel.setBounds (left.removeFromTop (24));

    r.removeFromLeft (4);
    auto mid = r.removeFromLeft (176).withSizeKeepingCentre (176, 28);
    if (! isAudio)
    {
        editButton.setBounds (mid.removeFromRight (40));
        mid.removeFromRight (3);
    }
    instrumentButton.setBounds (mid);

    r.removeFromLeft (6);
    autoButton.setBounds (r.removeFromLeft (26).withSizeKeepingCentre (26, 26));
    r.removeFromLeft (6);

    auto btns = r.removeFromLeft (3 * 29).withSizeKeepingCentre (3 * 29, 26);
    armButton.setBounds  (btns.removeFromLeft (27)); btns.removeFromLeft (2);
    muteButton.setBounds (btns.removeFromLeft (27)); btns.removeFromLeft (2);
    soloButton.setBounds (btns.removeFromLeft (27));

    panSlider.setBounds (r.removeFromRight (36));
    r.removeFromRight (2);
    volumeSlider.setBounds (r.withSizeKeepingCentre (r.getWidth(), 24));
}

void TrackRow::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect (*this);
}

//==============================================================================
// AutoLaneHeader
//==============================================================================
AutoLaneHeader::AutoLaneHeader (te::AudioTrack& t, te::AutomatableParameter::Ptr p, const juce::String& k,
                                const juce::String& ttl, juce::Colour c)
    : track (t), param (p), key (k), title (ttl), colour (c)
{
    for (auto* b : { &clearButton, &removeButton })
    {
        b->setColour (juce::TextButton::buttonColourId, colours::panelLight);
        addAndMakeVisible (b);
    }
    clearButton.setTooltip ("Delete all automation points in this lane");
    removeButton.setTooltip ("Hide this lane (the automation stays)");
    clearButton.onClick  = [this] { if (onClear) onClear (*this); };
    removeButton.onClick = [this] { if (onRemove) onRemove (*this); };
    tick();
}

void AutoLaneHeader::tick()
{
    auto v = param != nullptr ? param->getCurrentValueAsString() : juce::String();
    if (v != valueText) { valueText = v; repaint(); }
}

void AutoLaneHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colours::background.darker (0.2f));
    g.fillRect (r);
    g.setColour (colour.withAlpha (0.5f));
    g.fillRect (r.removeFromLeft (6));
    g.setColour (colours::automation);
    g.fillRect (r.removeFromLeft (3).reduced (0, 6).translated (8, 0));

    g.setColour (colours::text);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText (title, 24, 4, getWidth() - 170, 18, juce::Justification::centredLeft, true);

    g.setColour (colours::automation);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (valueText, 24, 22, getWidth() - 170, 16, juce::Justification::centredLeft, true);

    g.setColour (colours::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void AutoLaneHeader::resized()
{
    auto r = getLocalBounds().reduced (8, 10);
    removeButton.setBounds (r.removeFromRight (28));
    r.removeFromRight (4);
    clearButton.setBounds (r.removeFromRight (56));
}

//==============================================================================
// TracksArea
//==============================================================================
TracksArea::TracksArea()
{
    addAndMakeVisible (arrange);
}

void TracksArea::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (0, 0, headerWidth, rulerHeight);
    g.setColour (colours::dimText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("TRACKS   (drop audio files on the timeline to import)", 14, 0, headerWidth - 20, rulerHeight,
                juce::Justification::centredLeft);
}

void TracksArea::resized()
{
    int autoIndex = 0;
    for (auto& l : lanes)
    {
        if (l.isAuto())
        {
            if (auto* h = autoHeaders[autoIndex++])
                h->setBounds (0, l.y, headerWidth, l.h);
        }
        else if (auto* row = rows[l.trackIndex])
        {
            row->setBounds (0, l.y, headerWidth, l.h);
        }
    }

    arrange.setBounds (headerWidth + 1, 0, juce::jmax (100, getWidth() - headerWidth - 1), getHeight());
}

//==============================================================================
// MainComponent
//==============================================================================
MainComponent::MainComponent()
{
    setWantsKeyboardFocus (true);

    titleLabel.setText ("L-DAW", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (titleLabel);

    for (auto* b : { &playButton, &stopButton, &recordButton, &addTrackButton, &importButton, &demoButton, &audioButton,
                     &pluginsButton, &newButton, &openButton, &saveButton, &undoButton, &redoButton,
                     &mixerButton, &fullMixerButton })
    {
        b->setColour (juce::TextButton::buttonColourId, colours::panelLight);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    playButton.setColour (juce::TextButton::buttonOnColourId, colours::play);
    recordButton.setColour (juce::TextButton::buttonOnColourId, colours::record);
    mixerButton.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    fullMixerButton.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    mixerButton.setTooltip ("Show the mixer below the tracks (F3). Drag the bar above it to resize.");
    fullMixerButton.setTooltip ("Mixer in the whole window (F9)");
    importButton.setTooltip ("Import audio files onto the selected audio track (or a new one)");

    styleToggle (loopButton,  colours::accent);
    styleToggle (clickButton, colours::accent);
    styleToggle (autoReadButton,  colours::play);
    styleToggle (autoWriteButton, colours::record);
    autoReadButton.setTooltip ("Play back automation");
    autoWriteButton.setTooltip ("Record automation: press Play and move any knob/fader (plug-in or mixer)");
    for (auto* b : { &loopButton, &clickButton, &autoReadButton, &autoWriteButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    playButton.onClick      = [this] { togglePlay(); };
    stopButton.onClick      = [this] { stopOrReturnToZero(); };
    recordButton.onClick    = [this] { toggleRecord(); };
    loopButton.onClick      = [this] { if (edit) { edit->getTransport().looping = loopButton.getToggleState(); updateLoopRange(); } };
    clickButton.onClick     = [this] { if (edit) edit->clickTrackEnabled = clickButton.getToggleState(); };
    autoReadButton.onClick  = [this] { if (edit) edit->getAutomationRecordManager().setReadingAutomation (autoReadButton.getToggleState()); };
    autoWriteButton.onClick = [this]
    {
        if (edit == nullptr) return;
        edit->getAutomationRecordManager().setWritingAutomation (autoWriteButton.getToggleState());
        if (autoWriteButton.getToggleState())
            setStatus ("Automation write ON: press Play and move knobs/faders. Recorded lanes appear under the tracks.");
    };
    addTrackButton.onClick  = [this] { showAddTrackMenu(); };
    importButton.onClick    = [this] { importAudio (selectedTrack, 0.0); };
    demoButton.onClick      = [this] { showQuickStartMenu(); };
    audioButton.onClick     = [this] { showAudioSettings(); };
    pluginsButton.onClick   = [this] { showPluginScanner(); };
    newButton.onClick       = [this] { createNewEdit(); };
    openButton.onClick      = [this] { openProject(); };
    saveButton.onClick      = [this] { saveProject(); };
    undoButton.onClick      = [this] { if (edit) { edit->getUndoManager().undo(); rebuildTracks(); } };
    redoButton.onClick      = [this] { if (edit) { edit->getUndoManager().redo(); rebuildTracks(); } };
    mixerButton.onClick     = [this] { setMixerDocked (! mixerDocked); };
    fullMixerButton.onClick = [this] { setMixerFull (! mixerFull); };

    tempoSlider.setSliderStyle (juce::Slider::IncDecButtons);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 26);
    tempoSlider.setRange (40.0, 240.0, 1.0);
    tempoSlider.setValue (120.0, juce::dontSendNotification);
    tempoSlider.onValueChange = [this]
    {
        if (edit == nullptr) return;
        if (auto tempo = edit->tempoSequence.getTempo (0))
            tempo->setBpm (tempoSlider.getValue());
        updateLoopRange();
    };
    addAndMakeVisible (tempoSlider);

    for (int bars : { 1, 2, 4, 8, 16, 32 })
        loopLengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
    loopLengthBox.setSelectedId (4, juce::dontSendNotification);
    loopLengthBox.onChange = [this] { updateLoopRange(); };
    addAndMakeVisible (loopLengthBox);

    for (int bars : { 8, 16, 32, 64 })
        zoomBox.addItem (juce::String (bars) + " bars", bars);
    zoomBox.setSelectedId (16, juce::dontSendNotification);
    zoomBox.onChange = [this] { tracksArea.arrange.barsVisible = zoomBox.getSelectedId(); tracksArea.arrange.repaint(); };
    addAndMakeVisible (zoomBox);

    for (auto* l : { &tempoLabel, &loopLabel, &zoomLabel })
    {
        l->setColour (juce::Label::textColourId, colours::dimText);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    positionLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold)));
    positionLabel.setColour (juce::Label::textColourId, colours::accent);
    positionLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black);
    positionLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (positionLabel);

    statusLabel.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (statusLabel);

    auto& arr = tracksArea.arrange;
    arr.onLaneClicked  = [this] (int i) { editorPanel.wantsKeys = false; editorPanel.repaint(); selectTrack (i); };
    arr.onClipsChanged = [this] { tracksArea.arrange.repaint(); };
    arr.onOpenEditor   = [this] (te::EditItemID id, int mode) { openEditor (id, mode); };
    arr.onClipSelected = [this] (te::EditItemID id)
    {
        if (editorOpen && id.isValid() && findMidiClip (edit.get(), id) != nullptr)
            openEditor (id, -1);
    };
    arr.onImportAt     = [this] (int trackIndex, double beat) { importAudio (trackIndex, beat); };
    arr.onFilesDropped = [this] (juce::Array<juce::File> files, int trackIndex, double beat) { importFiles (files, trackIndex, beat); };
    arr.onClipTempo    = [this] (te::EditItemID id) { showClipTempoDialog (id); };

    editorPanel.onClose = [this] { closeEditor(); };
    addChildComponent (editorPanel);

    editorDivider.onDrag = [this] (int dy) { editorHeight -= dy; resized(); };
    mixerDivider.onDrag  = [this] (int dy) { mixerHeight  -= dy; resized(); };
    addChildComponent (editorDivider);
    addChildComponent (mixerDivider);

    viewport.setViewedComponent (&tracksArea, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    addChildComponent (mixerView);

    createNewEdit();

    setSize (1400, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    mixerView.setEdit (nullptr);
    tracksArea.rows.clear();
    tracksArea.autoHeaders.clear();
    tracksArea.lanes.clear();
    tracksArea.arrange.lanes.clear();
    tracksArea.arrange.edit = nullptr;
    editorPanel.setClip (nullptr, {}, false);

    if (edit != nullptr)
        edit->getTransport().stop (false, true);

    edit = nullptr;
}

//==============================================================================
// Project
//==============================================================================
void MainComponent::createNewEdit()
{
    auto file = projectFolder().getNonexistentChildFile ("Untitled", ".tracktionedit");
    auto newEdit = te::createEmptyEdit (engine, file);
    newEdit->playInStopEnabled = true;
    newEdit->ensureNumberOfAudioTracks (4);

    int n = 1;
    for (auto* t : te::getAudioTracks (*newEdit))
        t->setName ("Track " + juce::String (n++));

    if (auto tempo = newEdit->tempoSequence.getTempo (0))
        tempo->setBpm (120.0);

    newEdit->getTransport().looping = true;

    currentFile = file;
    setEdit (std::move (newEdit));
    autoLoadMyInstruments();
    setStatus ("New project. Arm a track (R) and play. Space = play/stop, F3 = mixer, A = automation lanes.");
}

void MainComponent::setEdit (std::unique_ptr<te::Edit> newEdit)
{
    mixerView.setEdit (nullptr);
    tracksArea.rows.clear();
    tracksArea.autoHeaders.clear();
    tracksArea.lanes.clear();
    tracksArea.arrange.lanes.clear();
    tracksArea.arrange.edit = nullptr;
    tracksArea.arrange.selectedClip = {};
    editorPanel.setClip (nullptr, {}, false);
    knownPointCounts.clear();
    lastMidiTarget = {};
    closeEditor();

    if (edit != nullptr)
        edit->getTransport().stop (false, true);

    edit = std::move (newEdit);
    selectedTrack = 0;
    armed = false;

    if (edit == nullptr)
        return;

    fx::ensureReturnTracks (*edit);
    fx::ensureMasterMeter (*edit);

    edit->playInStopEnabled = true;
    edit->getTransport().ensureContextAllocated();

    if (auto tempo = edit->tempoSequence.getTempo (0))
        tempoSlider.setValue (tempo->getBpm(), juce::dontSendNotification);

    loopButton.setToggleState (edit->getTransport().looping.get(), juce::dontSendNotification);
    clickButton.setToggleState (edit->clickTrackEnabled.get(), juce::dontSendNotification);
    autoReadButton.setToggleState (edit->getAutomationRecordManager().isReadingAutomation(), juce::dontSendNotification);
    autoWriteButton.setToggleState (edit->getAutomationRecordManager().isWritingAutomation(), juce::dontSendNotification);

    tracksArea.arrange.edit = edit.get();
    updateLoopRange();
    showRecordedAutomation (true);
    rebuildTracks();
    routeInputs();
    mixerView.setEdit (edit.get());
}

void MainComponent::saveProject()
{
    if (edit == nullptr) return;

    chooser = std::make_unique<juce::FileChooser> ("Save project", currentFile, "*.tracktionedit");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File() || edit == nullptr) return;

        f = f.withFileExtension ("tracktionedit");
        edit->editFileRetriever = [f] { return f; };

        if (te::EditFileOperations (*edit).save (true, true, false))
        {
            currentFile = f;
            setStatus ("Saved " + f.getFullPathName());
        }
        else
        {
            setStatus ("Could not save " + f.getFullPathName());
        }
    });
}

void MainComponent::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open project", projectFolder(), "*.tracktionedit");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;

        setEdit (nullptr);
        auto loaded = te::loadEditFromFile (engine, f);

        if (loaded == nullptr)
        {
            setStatus ("Could not open " + f.getFileName());
            createNewEdit();
            return;
        }

        currentFile = f;
        setEdit (std::move (loaded));
        setStatus ("Opened " + f.getFileName());
    });
}

//==============================================================================
// Tracks + lanes
//==============================================================================
te::AudioTrack* MainComponent::getTrack (int index) const
{
    if (edit == nullptr) return nullptr;
    auto tracks = getMusicTracks (*edit);
    return juce::isPositiveAndBelow (index, tracks.size()) ? tracks[index] : nullptr;
}

te::AudioTrack* MainComponent::findTrack (te::EditItemID id) const
{
    if (edit == nullptr || ! id.isValid()) return nullptr;
    for (auto* t : getMusicTracks (*edit))
        if (t->itemID == id)
            return t;
    return nullptr;
}

juce::String MainComponent::makeLaneSignature()
{
    juce::String s;
    if (edit != nullptr)
        for (auto* t : getMusicTracks (*edit))
            s << t->itemID.toString() << ":" << t->state.getProperty (autoLanesProp).toString()
              << ":" << t->pluginList.getPlugins().size() << ":" << t->state.getProperty (kindProp).toString() << ";";
    return s;
}

void MainComponent::rebuildTracks()
{
    tracksArea.rows.clear();
    tracksArea.autoHeaders.clear();
    tracksArea.lanes.clear();
    tracksArea.arrange.lanes.clear();

    if (edit == nullptr) return;

    auto tracks = getMusicTracks (*edit);
    selectedTrack = juce::jlimit (0, juce::jmax (0, tracks.size() - 1), selectedTrack);

    int y = rulerHeight;
    for (int i = 0; i < tracks.size(); ++i)
    {
        auto* t = tracks[i];
        const auto colour = trackColour (i);

        Lane lane;
        lane.track = t; lane.trackIndex = i; lane.y = y; lane.h = rowHeight;
        tracksArea.lanes.push_back (lane);
        y += rowHeight;

        auto* row = tracksArea.rows.add (new TrackRow (*t, i, colour));
        row->onSelect           = [this] (TrackRow& r) { editorPanel.wantsKeys = false; selectTrack (r.index); };
        row->onChooseInstrument = [this] (TrackRow& r) { selectTrack (r.index); chooseInstrument (r); };
        row->onChooseInput      = [this] (TrackRow& r) { selectTrack (r.index); chooseInput (r); };
        row->onOpenInstrument   = [this] (TrackRow& r) { selectTrack (r.index); openInstrumentWindow (r.track); };
        row->onAddAutomation    = [this] (TrackRow& r) { selectTrack (r.index); showAutomationMenu (r); };
        row->onArm = [this] (TrackRow& r, bool on) { armed = on; selectTrack (r.index); };
        row->setSelected (i == selectedTrack);
        row->setArmed (armed && i == selectedTrack);
        tracksArea.addAndMakeVisible (row);

        for (auto& key : getAutoLanes (*t))
        {
            auto* p = findParam (*t, key);
            if (p == nullptr) continue;

            Lane al;
            al.track = t; al.trackIndex = i; al.param = p; al.key = key; al.y = y; al.h = autoLaneHeight;
            tracksArea.lanes.push_back (al);
            y += autoLaneHeight;

            juce::String title = p->getParameterName();
            if (auto* plugin = findPluginByID (*t, key.upToFirstOccurrenceOf ("|", false, false)))
                title = pluginTitle (*plugin) + ":  " + title;

            auto* h = tracksArea.autoHeaders.add (new AutoLaneHeader (*t, p, key, title, colour));
            h->onRemove = [] (AutoLaneHeader& hd)
            {
                auto keys = getAutoLanes (hd.track);
                keys.removeString (hd.key);
                setAutoLanes (hd.track, keys);
            };
            h->onClear = [this] (AutoLaneHeader& hd)
            {
                if (edit == nullptr || hd.param == nullptr) return;
                edit->getUndoManager().beginNewTransaction();
                ldaw::curve::clear (hd.param->getCurve(), &edit->getUndoManager());
            };
            tracksArea.addAndMakeVisible (h);
        }
    }

    tracksArea.arrange.lanes = tracksArea.lanes;
    tracksArea.arrange.selectedTrack = selectedTrack;
    laneSignature = makeLaneSignature();
    resized();
    tracksArea.resized();
}

void MainComponent::selectTrack (int index)
{
    if (getTrack (index) == nullptr) return;

    selectedTrack = index;

    for (auto* r : tracksArea.rows)
    {
        r->setSelected (r->index == selectedTrack);
        r->setArmed (armed && r->index == selectedTrack);
    }

    tracksArea.arrange.selectedTrack = selectedTrack;
    tracksArea.arrange.repaint();
    routeInputs();
}

void MainComponent::routeInputs()
{
    if (edit == nullptr) return;

    auto* sel = getTrack (selectedTrack);
    auto& dm = engine.getDeviceManager();

    for (int i = 0; i < dm.getNumMidiInDevices(); ++i)
        if (auto mip = dm.getMidiInDevice (i))
        {
            mip->setMonitorMode (te::InputDevice::MonitorMode::on);
            mip->setEnabled (true);
        }

    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto w = dm.getWaveInDevice (i))
            w->setEnabled (true);

    if (sel != nullptr && ! isAudioTrack (*sel))
        lastMidiTarget = sel->itemID;
    auto* midiTarget = findTrack (lastMidiTarget);

    const bool armAudio = sel != nullptr && isAudioTrack (*sel) && armed;
    juce::String wantedInput;
    if (sel != nullptr && isAudioTrack (*sel))
    {
        wantedInput = sel->state.getProperty (inputProp).toString();
        if (wantedInput.isEmpty())
        {
            auto names = audio::getWaveInputNames (engine);
            if (! names.isEmpty()) wantedInput = names[0];
        }
    }

    edit->getTransport().ensureContextAllocated();

    int numMidiInputs = 0;
    bool waveAssigned = false;

    for (auto* inst : edit->getAllInputDevices())
    {
        const auto type = inst->getInputDevice().getDeviceType();

        if (type == te::InputDevice::physicalMidiDevice)
        {
            ++numMidiInputs;
            if (midiTarget == nullptr) continue;
            (void) inst->setTarget (midiTarget->itemID, true, nullptr, 0);
            inst->setRecordingEnabled (midiTarget->itemID, armed && sel == midiTarget);
        }
        else if (type == te::InputDevice::waveDevice)
        {
            for (auto id : inst->getTargets())
                (void) inst->removeTarget (id, nullptr);

            if (armAudio && ! waveAssigned
                && (wantedInput.isEmpty() || inst->getInputDevice().getName() == wantedInput))
            {
                (void) inst->setTarget (sel->itemID, true, nullptr, 0);
                inst->setRecordingEnabled (sel->itemID, true);
                waveAssigned = true;
            }
        }
    }

    edit->restartPlayback();

    if (armAudio)
        setStatus (waveAssigned ? "Audio track armed (input: " + wantedInput + "). Press Rec to record. Use headphones to avoid feedback."
                                : "No audio input found - pick an ASIO driver with inputs under Audio / MIDI.");
    else if (numMidiInputs == 0 && sel != nullptr && ! isAudioTrack (*sel))
        setStatus ("No MIDI keyboard found. Connect one and click the track again.");
}

te::AudioTrack* MainComponent::addTrack (bool audioTrack)
{
    if (edit == nullptr) return nullptr;

    auto music = getMusicTracks (*edit);
    te::Track* preceding = music.isEmpty() ? nullptr : static_cast<te::Track*> (music.getLast());

    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, preceding), nullptr);
    if (t == nullptr) return nullptr;

    if (audioTrack)
    {
        t->state.setProperty (kindProp, "audio", nullptr);
        t->setName ("Audio " + juce::String (music.size() + 1));
    }
    else
    {
        t->setName ("Track " + juce::String (music.size() + 1));
    }

    selectedTrack = music.size();
    rebuildTracks();
    selectTrack (selectedTrack);
    return t.get();
}

void MainComponent::showAddTrackMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Instrument track (MIDI)");
    m.addItem (2, "Audio track (record / import audio)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton),
                     [safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr || r == 0) return;
        safe->addTrack (r == 2);
    });
}

juce::Array<juce::PluginDescription> MainComponent::getInstrumentPlugins()
{
    juce::Array<juce::PluginDescription> list;
    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument)
            list.add (d);
    return list;
}

void MainComponent::chooseInstrument (TrackRow& row)
{
    auto list = getInstrumentPlugins();

    juce::PopupMenu m;
    for (int i = 0; i < list.size(); ++i)
        m.addItem (i + 1, list[i].name + "   (" + list[i].manufacturerName + ", " + list[i].pluginFormatName + ")");

    if (list.isEmpty())
        m.addItem (-1, "No instruments found - scan for plug-ins first", false);

    m.addSeparator();
    m.addItem (1000, "Remove instrument");
    m.addItem (1001, "Scan for plug-ins...");

    const auto trackID = row.track.itemID;

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&row),
                     [this, list, trackID, safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr || edit == nullptr || r == 0) return;

        auto* track = findTrack (trackID);
        if (track == nullptr) return;

        if (r == 1001)                          showPluginScanner();
        else if (r == 1000)                     removeInstruments (*track);
        else if (r >= 1 && r <= list.size())    loadInstrument (*track, list[r - 1]);

        for (auto* rw : tracksArea.rows)
            rw->refresh();
    });
}

void MainComponent::chooseInput (TrackRow& row)
{
    auto names = audio::getWaveInputNames (engine);
    const auto current = row.track.state.getProperty (inputProp).toString();

    juce::PopupMenu m;
    m.addSectionHeader ("Record from");
    for (int i = 0; i < names.size(); ++i)
        m.addItem (i + 1, names[i], true, names[i] == current || (current.isEmpty() && i == 0));
    if (names.isEmpty())
        m.addItem (-1, "No inputs - open Audio / MIDI and enable inputs", false);
    m.addSeparator();
    m.addItem (1000, "Audio / MIDI settings...");

    const auto trackID = row.track.itemID;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&row),
                     [this, names, trackID, safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1000) { showAudioSettings(); return; }

        if (auto* t = findTrack (trackID))
            if (r >= 1 && r <= names.size())
            {
                t->state.setProperty (inputProp, names[r - 1], nullptr);
                routeInputs();
            }
        for (auto* rw : tracksArea.rows)
            rw->refresh();
    });
}

void MainComponent::removeInstruments (te::AudioTrack& t)
{
    juce::Array<te::Plugin*> toRemove;
    for (auto p : t.pluginList.getPlugins())
        if (dynamic_cast<te::ExternalPlugin*> (&*p) != nullptr && p->isSynth())
            toRemove.add (&*p);

    for (auto* p : toRemove)
        p->deleteFromParent();
}

void MainComponent::loadInstrument (te::AudioTrack& t, const juce::PluginDescription& desc)
{
    removeInstruments (t);

    if (auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc))
    {
        t.pluginList.insertPlugin (plugin, 0, nullptr);
        setStatus ("Loaded " + desc.name + " on " + t.getName() + ".");
        edit->restartPlayback();
    }
    else
    {
        setStatus ("Could not load " + desc.name + ".");
    }
}

void MainComponent::openInstrumentWindow (te::AudioTrack& t)
{
    if (auto* ep = findInstrument (t))
        fx::openWindow (*ep);
    else
        setStatus ("No instrument on " + t.getName() + " yet - click the instrument button first.");
}

void MainComponent::autoLoadMyInstruments()
{
    auto list = getInstrumentPlugins();
    auto find = [&list] (juce::StringArray keys) -> const juce::PluginDescription*
    {
        for (auto& d : list)
            for (auto& k : keys)
                if (d.name.containsIgnoreCase (k))
                    return &d;
        return nullptr;
    };

    int loaded = 0;
    if (auto* jno = find ({ "JNO" }))
        if (auto* t = getTrack (0)) { loadInstrument (*t, *jno); t->setName ("JNO-600"); ++loaded; }

    if (auto* dr = find ({ "DR-1010", "DR1010", "DR 1010" }))
        if (auto* t = getTrack (1)) { loadInstrument (*t, *dr); t->setName ("DR-1010"); ++loaded; }

    for (auto* r : tracksArea.rows)
        r->refresh();

    if (loaded == 0 && list.isEmpty())
        setStatus ("No plug-ins known yet: click Plug-ins > Options > Scan for new or updated VST3 plug-ins.");
}

//==============================================================================
// Automation
//==============================================================================
void MainComponent::showAutomationMenu (TrackRow& row)
{
    auto& t = row.track;
    auto shown = getAutoLanes (t);

    juce::PopupMenu m;
    juce::StringArray keys;

    for (auto p : t.pluginList.getPlugins())
    {
        if (! isAutomatablePlugin (*p)) continue;

        juce::PopupMenu sub;
        for (int i = 0; i < p->getNumAutomatableParameters(); ++i)
            if (auto a = p->getAutomatableParameter (i))
            {
                const auto key = paramKey (*p, *a);
                keys.add (key);
                const bool hasData = a->getCurve().getNumPoints() > 0;
                sub.addItem (keys.size(), a->getParameterName() + (hasData ? "   *" : ""), true, shown.contains (key));
            }

        m.addSubMenu (pluginTitle (*p), sub);
    }

    m.addSeparator();
    m.addItem (-2, "Show all lanes with recorded automation");
    m.addItem (-3, "Hide all automation lanes");

    const auto trackID = t.itemID;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&row),
                     [this, keys, trackID, safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto* track = findTrack (trackID);
        if (track == nullptr) return;

        auto lanes = getAutoLanes (*track);

        if (r == -2)
        {
            for (auto p : track->pluginList.getPlugins())
                for (int i = 0; i < p->getNumAutomatableParameters(); ++i)
                    if (auto a = p->getAutomatableParameter (i))
                        if (a->getCurve().getNumPoints() > 0)
                            lanes.addIfNotAlreadyThere (paramKey (*p, *a));
        }
        else if (r == -3)
        {
            lanes.clear();
        }
        else if (r >= 1 && r <= keys.size())
        {
            const auto key = keys[r - 1];
            if (lanes.contains (key)) lanes.removeString (key);
            else                      lanes.add (key);
        }

        setAutoLanes (*track, lanes);
        rebuildTracks();
    });
}

void MainComponent::showRecordedAutomation (bool all)
{
    if (edit == nullptr) return;

    for (auto* t : getMusicTracks (*edit))
    {
        auto lanes = getAutoLanes (*t);
        bool changed = false;

        for (auto p : t->pluginList.getPlugins())
            for (int i = 0; i < p->getNumAutomatableParameters(); ++i)
                if (auto a = p->getAutomatableParameter (i))
                {
                    const auto key = paramKey (*p, *a);
                    const int count = a->getCurve().getNumPoints();
                    auto it = knownPointCounts.find (key);
                    const int before = it != knownPointCounts.end() ? it->second : 0;

                    if (count > 0 && (all || count > before) && ! lanes.contains (key))
                    {
                        lanes.add (key);
                        changed = true;
                    }
                    knownPointCounts[key] = count;
                }

        if (changed)
            setAutoLanes (*t, lanes);
    }
}

//==============================================================================
// Audio import / tempo
//==============================================================================
void MainComponent::importAudio (int trackIndex, double beat)
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   audio::fileWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, trackIndex, beat] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();
        if (! files.isEmpty())
            importFiles (files, trackIndex, beat);
    });
}

void MainComponent::importFiles (juce::Array<juce::File> files, int trackIndex, double beat)
{
    if (edit == nullptr || files.isEmpty()) return;

    const double bpmNow = projectBpm (*edit);
    const double guess = audio::guessBpm (files.getFirst(), bpmNow);

    auto* target = getTrack (trackIndex);
    const auto targetID = (target != nullptr && isAudioTrack (*target)) ? target->itemID : te::EditItemID();

    auto* aw = new juce::AlertWindow ("Import audio",
                                      juce::String (files.size()) + " file(s). If the loop was made at another tempo, enter it "
                                      "and L-DAW will time-stretch it to follow the project tempo ("
                                      + juce::String (bpmNow, 1) + " BPM).",
                                      juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor ("bpm", juce::String (guess, 1), "Original tempo of the file (BPM):");
    aw->addComboBox ("mode", { "Follow project tempo (time-stretch)", "Keep original speed (no stretch)" }, "Timing:");
    aw->addButton ("Import", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, files, targetID, beat] (int result)
    {
        if (safe == nullptr || result != 1 || safe->edit == nullptr) return;

        const double srcBpm = juce::jlimit (20.0, 300.0, aw->getTextEditorContents ("bpm").getDoubleValue());
        const bool follow = aw->getComboBoxComponent ("mode")->getSelectedItemIndex() == 0;

        auto* track = safe->findTrack (targetID);
        if (track == nullptr)
            track = safe->addTrack (true);
        if (track == nullptr) return;

        auto& e = *safe->edit;
        e.getUndoManager().beginNewTransaction();
        double at = beat;
        int imported = 0;

        for (auto& f : files)
        {
            if (auto* clip = audio::importFile (*track, f, timeOf (e, at), srcBpm, follow))
            {
                at = std::ceil (beatOf (e, clip->getPosition().getEnd()) - 1e-6);
                ++imported;
            }
        }

        safe->setStatus ("Imported " + juce::String (imported) + " file(s) on " + track->getName()
                         + (follow ? " - following the project tempo." : "."));
        safe->tracksArea.arrange.repaint();
    }), true);
}

void MainComponent::showClipTempoDialog (te::EditItemID id)
{
    if (edit == nullptr) return;
    auto* clip = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (*edit, id));
    if (clip == nullptr) return;

    double bpm = audio::getSourceBpm (*clip);
    if (bpm <= 0.0) bpm = audio::guessBpm (clip->getOriginalFile(), projectBpm (*edit));

    auto* aw = new juce::AlertWindow ("Tempo / time-stretch", clip->getName(), juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor ("bpm", juce::String (bpm, 1), "Original tempo of the file (BPM):");
    aw->addComboBox ("mode", { "Follow project tempo (time-stretch)", "Keep original speed (no stretch)" }, "Timing:");
    aw->getComboBoxComponent ("mode")->setSelectedItemIndex (clip->getAutoTempo() ? 0 : 1);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, id] (int result)
    {
        if (safe == nullptr || result != 1 || safe->edit == nullptr) return;
        auto* c = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (*safe->edit, id));
        if (c == nullptr) return;

        const double srcBpm = juce::jlimit (20.0, 300.0, aw->getTextEditorContents ("bpm").getDoubleValue());
        const bool follow = aw->getComboBoxComponent ("mode")->getSelectedItemIndex() == 0;

        safe->edit->getUndoManager().beginNewTransaction();
        audio::setTempoFollow (*c, srcBpm, follow);
        safe->tracksArea.arrange.repaint();
    }), true);
}

//==============================================================================
// Transport
//==============================================================================
void MainComponent::updateLoopRange()
{
    if (edit == nullptr) return;

    const int bars = juce::jmax (1, loopLengthBox.getSelectedId());
    edit->getTransport().setLoopRange (te::TimeRange (timeOf (*edit, 0.0), timeOf (*edit, bars * 4.0)));
    tracksArea.arrange.repaint();
}

void MainComponent::togglePlay()
{
    if (edit == nullptr) return;
    auto& tc = edit->getTransport();

    if (tc.isPlaying()) tc.stop (false, false);
    else                tc.play (false);
}

void MainComponent::stopOrReturnToZero()
{
    if (edit == nullptr) return;
    auto& tc = edit->getTransport();

    if (tc.isPlaying() || tc.isRecording())
        tc.stop (false, false);
    else
        tc.setPosition (te::TimePosition());

    tracksArea.arrange.repaint();
}

void MainComponent::toggleRecord()
{
    if (edit == nullptr) return;
    auto& tc = edit->getTransport();

    if (tc.isRecording())
    {
        tc.stop (false, false);
        juce::Timer::callAfterDelay (400, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr) safe->tracksArea.arrange.repaint();
        });
        return;
    }

    if (! armed)
    {
        armed = true;
        selectTrack (selectedTrack);
        if (auto* t = getTrack (selectedTrack))
            setStatus ("Armed " + t->getName() + " for recording.");
    }

    tc.record (false);
}

//==============================================================================
// Quick start
//==============================================================================
void MainComponent::showQuickStartMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Load JNO-600 + DR-1010 on tracks 1-2");
    m.addSeparator();
    m.addSectionHeader ("Insert 4-bar demo clip on selected track");
    m.addItem (11, "Chords (Am - F - C - G)");
    m.addItem (12, "Bass line");
    m.addItem (13, "Drum beat (GM notes 36/38/42)");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&demoButton),
                     [this, safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)       { autoLoadMyInstruments(); if (getInstrumentPlugins().isEmpty()) showPluginScanner(); }
        else if (r >= 11) insertDemoPattern (r - 10);
    });
}

void MainComponent::insertDemoPattern (int type)
{
    auto* track = getTrack (selectedTrack);
    if (track == nullptr) return;
    if (isAudioTrack (*track)) { setStatus ("Select an instrument track for demo MIDI clips."); return; }

    double startBeat = 0.0;
    for (auto* c : track->getClips())
        startBeat = juce::jmax (startBeat, beatOf (*edit, c->getPosition().getEnd()));
    startBeat = std::ceil (startBeat / 4.0) * 4.0;

    const te::TimeRange range (timeOf (*edit, startBeat), timeOf (*edit, startBeat + 16.0));

    edit->getUndoManager().beginNewTransaction();
    auto clip = track->insertMIDIClip (range, nullptr);
    if (clip == nullptr) return;

    auto& seq = clip->getSequence();
    auto add = [&seq] (int note, double beat, double len, int vel)
    {
        seq.addNote (note, te::BeatPosition::fromBeats (beat), te::BeatDuration::fromBeats (len), vel, 0, nullptr);
    };

    if (type == 1)
    {
        clip->setName ("Chords");
        const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
        for (int bar = 0; bar < 4; ++bar)
            for (int n : chords[bar])
                add (n, bar * 4.0, 3.9, 90);
    }
    else if (type == 2)
    {
        clip->setName ("Bass");
        const int roots[4] = { 33, 29, 36, 31 };
        const double pattern[] = { 0.0, 0.75, 1.5, 2.0, 2.75, 3.5 };
        for (int bar = 0; bar < 4; ++bar)
            for (double p : pattern)
                add (roots[bar] + (p == 2.75 ? 12 : 0), bar * 4.0 + p, 0.4, 100);
    }
    else
    {
        clip->setName ("Drums");
        for (int bar = 0; bar < 4; ++bar)
        {
            const double b = bar * 4.0;
            for (double k : { 0.0, 1.5, 2.0 })  add (36, b + k, 0.25, 115);
            for (double s : { 1.0, 3.0 })       add (38, b + s, 0.25, 105);
            for (int h = 0; h < 8; ++h)         add (42, b + h * 0.5, 0.2, h % 2 == 0 ? 95 : 70);
        }
    }

    const int neededBars = (int) ((startBeat + 16.0) / 4.0);
    if (neededBars > loopLengthBox.getSelectedId())
    {
        for (int bars : { 1, 2, 4, 8, 16, 32 })
            if (bars >= neededBars) { loopLengthBox.setSelectedId (bars); break; }
    }

    tracksArea.arrange.selectedClip = clip->itemID;
    openEditor (clip->itemID, type == 3 ? 2 : 1);
    setStatus ("Inserted demo clip on " + track->getName() + ". Press Play (or Space).");
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager,
                                                             0, 8, 0, 8, false, false, true, false);
    selector->setSize (560, 460);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio settings (choose ASIO for lowest latency, enable inputs for recording)";
    o.dialogBackgroundColour = colours::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showPluginScanner()
{
    auto& pm = engine.getPluginManager();
    auto deadMans = projectFolder().getChildFile ("RecentlyCrashedPluginsList");

    auto* list = new juce::PluginListComponent (pm.pluginFormatManager, pm.knownPluginList, deadMans, nullptr);
    list->setSize (760, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Plug-ins  -  Options > Scan for new or updated VST3 plug-ins";
    o.dialogBackgroundColour = colours::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

//==============================================================================
// Panels
//==============================================================================
void MainComponent::openEditor (te::EditItemID id, int mode)
{
    auto* mc = findMidiClip (edit.get(), id);
    if (mc == nullptr)
    {
        setStatus ("Select a MIDI clip first (double-click an empty spot in an instrument track to create one).");
        return;
    }

    bool step = false;
    if (mode == 2)       step = true;
    else if (mode == 1)  step = false;
    else if (mode == -1) step = editorPanel.isStepMode();
    else
    {
        juce::String names = mc->getName() + " " + mc->getTrack()->getName();
        if (auto* at = dynamic_cast<te::AudioTrack*> (mc->getTrack()))
            for (auto p : at->pluginList.getPlugins())
                names << " " << p->getName();
        step = names.containsIgnoreCase ("DR-") || names.containsIgnoreCase ("DR1") || names.containsIgnoreCase ("drum");
    }

    tracksArea.arrange.selectedClip = id;
    editorPanel.setClip (edit.get(), id, step);
    editorOpen = true;
    editorPanel.wantsKeys = true;
    if (mixerFull) setMixerFull (false);
    resized();
}

void MainComponent::closeEditor()
{
    editorOpen = false;
    editorPanel.wantsKeys = false;
    resized();
}

void MainComponent::setMixerDocked (bool docked)
{
    mixerDocked = docked;
    if (docked) mixerFull = false;
    if (mixerDocked || mixerFull) mixerView.rebuild();
    resized();
}

void MainComponent::setMixerFull (bool full)
{
    mixerFull = full;
    if (mixerDocked || mixerFull) mixerView.rebuild();
    resized();
}

void MainComponent::setStatus (const juce::String& s)
{
    statusLabel.setText (s, juce::dontSendNotification);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (editorOpen && editorPanel.wantsKeys && editorPanel.handleKey (k))
        return true;

    const auto cmd = juce::ModifierKeys::commandModifier;

    if (k == juce::KeyPress::spaceKey)               { togglePlay(); return true; }
    if (k == juce::KeyPress::returnKey)              { stopOrReturnToZero(); return true; }
    if (k == juce::KeyPress::F3Key)                  { setMixerDocked (! mixerDocked); return true; }
    if (k == juce::KeyPress::F9Key)                  { setMixerFull (! mixerFull); return true; }
    if (k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R') { toggleRecord(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { tracksArea.arrange.deleteSelectedClip(); return true; }
    if (k == juce::KeyPress ('d', cmd, 0)) { tracksArea.arrange.duplicateSelectedClip(); return true; }
    if (k == juce::KeyPress ('z', cmd, 0)) { undoButton.triggerClick(); return true; }
    if (k == juce::KeyPress ('y', cmd, 0)) { redoButton.triggerClick(); return true; }
    if (k == juce::KeyPress ('e', cmd, 0)) { editorOpen ? closeEditor() : openEditor (tracksArea.arrange.selectedClip, 0); return true; }
    return false;
}

void MainComponent::timerCallback()
{
    if (edit == nullptr) return;

    auto& tc = edit->getTransport();

    const double beats = beatOf (*edit, tc.getPosition());
    const int bar  = (int) std::floor (beats / 4.0) + 1;
    const int beat = (int) std::floor (std::fmod (beats, 4.0)) + 1;
    const int tick = (int) std::floor (std::fmod (beats, 1.0) * 100.0);
    positionLabel.setText (juce::String (bar).paddedLeft ('0', 3) + " . " + juce::String (beat)
                             + " . " + juce::String (tick).paddedLeft ('0', 2),
                           juce::dontSendNotification);

    playButton.setToggleState (tc.isPlaying(), juce::dontSendNotification);
    recordButton.setToggleState (tc.isRecording(), juce::dontSendNotification);

    ++tickCount;

    if (! mixerFull)
    {
        tracksArea.arrange.repaint();
        for (auto* h : tracksArea.autoHeaders) h->tick();
        if (tickCount % 10 == 0)
            for (auto* r : tracksArea.rows) r->refresh();
    }

    if ((mixerDocked || mixerFull) && tickCount % 2 == 0)
        mixerView.tick();

    if (tickCount % 15 == 0)
    {
        showRecordedAutomation (false);
        if (makeLaneSignature() != laneSignature)
            rebuildTracks();
    }

    editorPanel.tick();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::panel);
    g.fillRect (0, 0, getWidth(), 96);
    g.setColour (colours::accent);
    g.fillRect (0, 94, getWidth(), 2);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (96).reduced (10, 8);

    auto row1 = top.removeFromTop (38);
    titleLabel.setBounds (row1.removeFromLeft (84));
    for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton, &clickButton })
        b->setBounds (row1.removeFromLeft (58).reduced (2));
    row1.removeFromLeft (8);
    positionLabel.setBounds (row1.removeFromLeft (164).reduced (2));
    row1.removeFromLeft (8);
    tempoLabel.setBounds (row1.removeFromLeft (38));
    tempoSlider.setBounds (row1.removeFromLeft (126).reduced (2, 4));
    loopLabel.setBounds (row1.removeFromLeft (40));
    loopLengthBox.setBounds (row1.removeFromLeft (86).reduced (2, 6));
    zoomLabel.setBounds (row1.removeFromLeft (40));
    zoomBox.setBounds (row1.removeFromLeft (86).reduced (2, 6));
    row1.removeFromLeft (10);
    autoReadButton.setBounds (row1.removeFromLeft (70).reduced (2));
    autoWriteButton.setBounds (row1.removeFromLeft (74).reduced (2));

    top.removeFromTop (6);

    auto row2 = top.removeFromTop (32);
    for (auto* b : { &newButton, &openButton, &saveButton, &undoButton, &redoButton })
        b->setBounds (row2.removeFromLeft (56).reduced (2));
    row2.removeFromLeft (8);
    for (auto* b : { &addTrackButton, &importButton, &demoButton, &pluginsButton, &audioButton })
        b->setBounds (row2.removeFromLeft (98).reduced (2));
    row2.removeFromLeft (8);
    mixerButton.setBounds (row2.removeFromLeft (70).reduced (2));
    fullMixerButton.setBounds (row2.removeFromLeft (86).reduced (2));
    row2.removeFromLeft (8);
    statusLabel.setBounds (row2);

    mixerButton.setToggleState (mixerDocked && ! mixerFull, juce::dontSendNotification);
    fullMixerButton.setToggleState (mixerFull, juce::dontSendNotification);

    if (mixerFull)
    {
        mixerView.setVisible (true);
        mixerView.setBounds (r);
        viewport.setVisible (false);
        editorPanel.setVisible (false);
        editorDivider.setVisible (false);
        mixerDivider.setVisible (false);
        return;
    }

    viewport.setVisible (true);

    mixerView.setVisible (mixerDocked);
    mixerDivider.setVisible (mixerDocked);
    if (mixerDocked)
    {
        mixerHeight = juce::jlimit (240, juce::jmax (240, r.getHeight() - 160), mixerHeight);
        mixerView.setBounds (r.removeFromBottom (mixerHeight));
        mixerDivider.setBounds (r.removeFromBottom (7));
    }

    editorPanel.setVisible (editorOpen);
    editorDivider.setVisible (editorOpen);
    if (editorOpen)
    {
        editorHeight = juce::jlimit (180, juce::jmax (180, r.getHeight() - 120), editorHeight);
        editorPanel.setBounds (r.removeFromBottom (editorHeight));
        editorDivider.setBounds (r.removeFromBottom (7));
    }

    viewport.setBounds (r);
    int contentHeight = rulerHeight + 40;
    for (auto& l : tracksArea.lanes) contentHeight = juce::jmax (contentHeight, l.y + l.h + 40);
    tracksArea.setBounds (0, 0, viewport.getMaximumVisibleWidth(), juce::jmax (contentHeight, r.getHeight()));
}
