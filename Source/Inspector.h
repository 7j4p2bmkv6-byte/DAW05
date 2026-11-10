#pragma once
// L-DAW 0.6: inspector panel for the selected track / clip
#include "Theme.h"
#include "Effects.h"
#include "Tools.h"
#include "AudioTracks.h"

class InspectorPanel : public juce::Component
{
public:
    InspectorPanel();

    void setTarget (te::Edit*, te::AudioTrack*, int trackIndex, te::EditItemID clipID);
    void tick();

    std::function<void (te::AudioTrack&)> onOpenInstrument, onShowInMixer;
    std::function<void (te::EditItemID)>  onEditClip, onClipTempo;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    te::Edit* edit = nullptr;
    te::EditItemID trackID, clipID;
    int trackIndex = 0;

    juce::Label trackName, clipName;
    juce::TextButton instrumentButton, outputButton, mixerButton { "Show in mixer / FX rack" },
                     editClipButton { "Edit notes" }, tempoButton { "Tempo / time-stretch..." };
    juce::Slider volume, pan;
    juce::String typeText, insertsText, clipText;
    juce::Rectangle<int> trackHeader, effectsArea, clipHeader, clipInfoArea;

    te::AudioTrack* track() const;
    te::Clip* clip() const;
    void refreshTexts();
};
