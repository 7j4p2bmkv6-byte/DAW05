#pragma once
// L-DAW 0.5
#include <JuceHeader.h>
#include "EngineUI.h"
#include "Theme.h"
#include "Effects.h"
#include "AudioTracks.h"
#include "ArrangeView.h"
#include "MidiEditors.h"
#include "MixerView.h"

using namespace ldaw;

//==============================================================================
// Track header: name, instrument / audio input, arm, mute, solo, automation, volume, pan
//==============================================================================
class TrackRow : public juce::Component
{
public:
    TrackRow (te::AudioTrack& t, int index, juce::Colour);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void refresh();
    void setSelected (bool);
    void setArmed (bool);

    te::AudioTrack& track;
    const int index;
    const bool isAudio;

    std::function<void (TrackRow&)>       onSelect;
    std::function<void (TrackRow&)>       onChooseInstrument;
    std::function<void (TrackRow&)>       onChooseInput;
    std::function<void (TrackRow&)>       onOpenInstrument;
    std::function<void (TrackRow&)>       onAddAutomation;
    std::function<void (TrackRow&, bool)> onArm;

private:
    bool selected = false;
    juce::Colour colour;
    juce::Label      nameLabel;
    juce::TextButton instrumentButton, editButton { "Edit" }, autoButton { "A" },
                     armButton { "R" }, muteButton { "M" }, soloButton { "S" };
    juce::Slider     volumeSlider, panSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackRow)
};

//==============================================================================
class AutoLaneHeader : public juce::Component
{
public:
    AutoLaneHeader (te::AudioTrack&, te::AutomatableParameter::Ptr, const juce::String& key,
                    const juce::String& title, juce::Colour);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    te::AudioTrack& track;
    te::AutomatableParameter::Ptr param;
    const juce::String key;

    std::function<void (AutoLaneHeader&)> onRemove, onClear;

private:
    juce::String title, valueText;
    juce::Colour colour;
    juce::TextButton clearButton { "Clear" }, removeButton { "X" };
};

//==============================================================================
class DividerBar : public juce::Component
{
public:
    DividerBar() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }

    std::function<void (int deltaY)> onDrag;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff111214));
        g.setColour (juce::Colour (0xff4a4c52));
        const int cx = getWidth() / 2;
        for (int i = -2; i <= 2; ++i)
            g.fillEllipse ((float) (cx + i * 8) - 1.5f, (float) getHeight() * 0.5f - 1.5f, 3.0f, 3.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { lastY = e.getScreenY(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dy = e.getScreenY() - lastY;
        lastY = e.getScreenY();
        if (dy != 0 && onDrag) onDrag (dy);
    }

private:
    int lastY = 0;
};

//==============================================================================
class TracksArea : public juce::Component
{
public:
    TracksArea();
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::OwnedArray<TrackRow> rows;
    juce::OwnedArray<AutoLaneHeader> autoHeaders;
    std::vector<ldaw::Lane> lanes;
    ArrangeView arrange;
};

//==============================================================================
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    te::Engine engine { "L-DAW", std::make_unique<AppUIBehaviour>(), nullptr };
    std::unique_ptr<te::Edit> edit;
    juce::File currentFile;

    int  selectedTrack = 0;
    bool armed = false;
    bool mixerDocked = false, mixerFull = false;
    int  editorHeight = 320, mixerHeight = 360;
    int  tickCount = 0;
    te::EditItemID lastMidiTarget;
    juce::String laneSignature;
    std::map<juce::String, int> knownPointCounts;

    juce::TooltipWindow tooltips { this, 700 };

    juce::TextButton playButton { "Play" }, stopButton { "Stop" }, recordButton { "Rec" },
                     loopButton { "Loop" }, clickButton { "Click" },
                     autoReadButton { "A.Read" }, autoWriteButton { "A.Write" },
                     addTrackButton { "+ Track" }, importButton { "Import audio" }, demoButton { "Quick start" },
                     audioButton { "Audio / MIDI" }, pluginsButton { "Plug-ins" },
                     newButton { "New" }, openButton { "Open" }, saveButton { "Save" },
                     undoButton { "Undo" }, redoButton { "Redo" },
                     mixerButton { "Mixer" }, fullMixerButton { "Full mixer" };
    juce::Slider     tempoSlider;
    juce::ComboBox   loopLengthBox, zoomBox;
    juce::Label      tempoLabel { {}, "BPM" }, loopLabel { {}, "Loop" }, zoomLabel { {}, "View" },
                     positionLabel, statusLabel, titleLabel;
    juce::Viewport   viewport;
    TracksArea       tracksArea;
    MixerView        mixerView;
    MidiEditorPanel  editorPanel;
    DividerBar       editorDivider, mixerDivider;
    bool             editorOpen = false;
    std::unique_ptr<juce::FileChooser> chooser;

    void createNewEdit();
    void setEdit (std::unique_ptr<te::Edit>);
    void saveProject();
    void openProject();

    void rebuildTracks();
    juce::String makeLaneSignature();
    void selectTrack (int index);
    void routeInputs();
    te::AudioTrack* addTrack (bool audio);
    void showAddTrackMenu();
    void chooseInstrument (TrackRow&);
    void chooseInput (TrackRow&);
    void openInstrumentWindow (te::AudioTrack&);
    void loadInstrument (te::AudioTrack&, const juce::PluginDescription&);
    void removeInstruments (te::AudioTrack&);
    te::AudioTrack* getTrack (int index) const;
    te::AudioTrack* findTrack (te::EditItemID) const;
    juce::Array<juce::PluginDescription> getInstrumentPlugins();

    void showAutomationMenu (TrackRow&);
    void showRecordedAutomation (bool all);

    void importAudio (int trackIndex, double beat);
    void importFiles (juce::Array<juce::File>, int trackIndex, double beat);
    void showClipTempoDialog (te::EditItemID);

    void updateLoopRange();
    void togglePlay();
    void stopOrReturnToZero();
    void toggleRecord();

    void showQuickStartMenu();
    void insertDemoPattern (int type);
    void autoLoadMyInstruments();
    void showAudioSettings();
    void showPluginScanner();
    void setStatus (const juce::String&);
    void openEditor (te::EditItemID, int mode);
    void closeEditor();
    void setMixerDocked (bool);
    void setMixerFull (bool);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
