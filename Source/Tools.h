#pragma once
// L-DAW 0.6: buses (group channels), export, count-in
#include "Theme.h"

namespace ldaw::routing
{
    // Where a track's audio goes: a bus track, or nullptr = master
    te::AudioTrack* outputBus (te::AudioTrack&);
    void setOutput (te::AudioTrack&, te::AudioTrack* busOrNull);
    juce::String outputName (te::AudioTrack&);

    te::AudioTrack* createBus (te::Edit&, const juce::String& name);
    void deleteBus (te::Edit&, te::AudioTrack& bus);

    // Popup: Master / every bus / New bus...
    void showOutputMenu (te::Edit&, te::AudioTrack&, juce::Component* target, std::function<void()> onDone);
}

namespace ldaw::tools
{
    // Renders the song (or loop) to a WAV file. Reports progress/result via status.
    void showExportDialog (te::Edit&, std::function<void (const juce::String&)> status);

    void setCountIn (te::Edit&, bool oneBar);
    bool getCountIn (te::Edit&);
}
