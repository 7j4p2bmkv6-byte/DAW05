#pragma once
// L-DAW 0.5
#include "Theme.h"

//==============================================================================
// Audio tracks: import, tempo-follow (time-stretch), copying, inputs
//==============================================================================
namespace ldaw::audio
{
    juce::String fileWildcard();
    bool isAudioFile (const juce::File&);

    // Reads "120bpm" / "120 BPM" / "_120_" style tempos from a file name
    double guessBpm (const juce::File&, double fallback);

    te::WaveAudioClip* importFile (te::AudioTrack&, const juce::File&, te::TimePosition start,
                                   double sourceBpm, bool followTempo);

    void setTempoFollow (te::AudioClipBase&, double sourceBpm, bool follow);
    double getSourceBpm (te::Clip&);

    te::Clip* copyClipTo (te::Clip&, te::AudioTrack& dest, te::TimePosition start);

    juce::StringArray getWaveInputNames (te::Engine&);
}
