#pragma once
// L-DAW 0.5
#include "Theme.h"
#include "AudioTracks.h"

//==============================================================================
// Timeline: ruler, loop, MIDI + audio clips, automation lanes, playhead
//==============================================================================
class ArrangeView : public juce::Component,
                    public juce::FileDragAndDropTarget
{
public:
    ArrangeView();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    void deleteSelectedClip();
    void duplicateSelectedClip();

    te::Edit* edit = nullptr;
    int selectedTrack = 0;
    int barsVisible = 16;
    te::EditItemID selectedClip;
    std::vector<ldaw::Lane> lanes;

    std::function<void (int)>                 onLaneClicked;
    std::function<void (te::EditItemID)>      onClipSelected;
    std::function<void (te::EditItemID, int)> onOpenEditor;     // 0 auto, 1 piano roll, 2 step
    std::function<void()>                     onClipsChanged;
    std::function<void (int, double)>         onImportAt;       // track index, beat
    std::function<void (juce::Array<juce::File>, int, double)> onFilesDropped;
    std::function<void (te::EditItemID)>      onClipTempo;

private:
    struct DragState
    {
        bool active = false, moved = false, resize = false, copy = false;
        te::EditItemID clipID;
        int track0 = 0, newTrack = 0;
        double start0 = 0, end0 = 0, downBeat = 0, newStart = 0, newEnd = 0;
    } drag;

    int autoLane = -1, autoPoint = -1;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbCache { 64 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbs;

    double pixelsPerBeat() const;
    double xToBeats (float x) const;
    float  beatsToX (double beats) const;
    int    laneIndexAt (int y) const;
    int    trackIndexAt (int y) const;
    int    numTracks() const;
    int    laneYForTrack (int trackIndex) const;
    te::Clip* clipAt (juce::Point<int>, int& trackIndex) const;
    void   applyDrag();
    void   showClipMenu (te::EditItemID);
    void   select (te::EditItemID);

    juce::AudioThumbnail* getThumb (const juce::File&);
    void drawAudioClip (juce::Graphics&, te::WaveAudioClip&, juce::Rectangle<float>);
    void drawAutomationLane (juce::Graphics&, const ldaw::Lane&);
    float valueToY (const ldaw::Lane&, float value) const;
    float yToValue (const ldaw::Lane&, float y) const;
    int   pointNear (const ldaw::Lane&, juce::Point<float>) const;
};
