#include "AudioTracks.h"

namespace ldaw::audio
{
    juce::String fileWildcard()
    {
        return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
    }

    bool isAudioFile (const juce::File& f)
    {
        return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
    }

    double guessBpm (const juce::File& f, double fallback)
    {
        const auto name = f.getFileNameWithoutExtension().toLowerCase();

        auto readNumberBefore = [&name] (int pos) -> double
        {
            int end = pos;
            while (end > 0 && (name[end - 1] == ' ' || name[end - 1] == '_' || name[end - 1] == '-')) --end;
            int start = end;
            while (start > 0 && juce::CharacterFunctions::isDigit (name[start - 1])) --start;
            const auto num = name.substring (start, end).getDoubleValue();
            return (num >= 40.0 && num <= 250.0) ? num : 0.0;
        };

        const int bpmPos = name.indexOf ("bpm");
        if (bpmPos > 0)
            if (auto v = readNumberBefore (bpmPos); v > 0.0)
                return v;

        auto tokens = juce::StringArray::fromTokens (name, " _-.", "");
        for (auto& t : tokens)
            if (t.containsOnly ("0123456789"))
                if (auto v = t.getDoubleValue(); v >= 60.0 && v <= 200.0)
                    return v;

        return fallback;
    }

    double getSourceBpm (te::Clip& c)
    {
        return (double) c.state.getProperty (sourceBpmProp, 0.0);
    }

    void setTempoFollow (te::AudioClipBase& c, double sourceBpm, bool follow)
    {
        c.state.setProperty (sourceBpmProp, sourceBpm, nullptr);

        if (follow && sourceBpm > 0.0)
        {
            c.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
            c.getLoopInfo().setBpm (sourceBpm, c.getAudioFile().getInfo());
            c.setAutoTempo (true);
        }
        else
        {
            c.setAutoTempo (false);
        }
    }

    te::WaveAudioClip* importFile (te::AudioTrack& track, const juce::File& file, te::TimePosition start,
                                   double sourceBpm, bool followTempo)
    {
        te::AudioFile af (track.edit.engine, file);
        const double len = af.getLength();
        if (len <= 0.0) return nullptr;

        te::ClipPosition pos;
        pos.time = te::TimeRange (start, start + te::TimeDuration::fromSeconds (len));

        auto clip = track.insertWaveClip (file.getFileNameWithoutExtension(), file, pos, false);
        if (clip == nullptr) return nullptr;

        setTempoFollow (*clip, sourceBpm, followTempo);
        return clip.get();
    }

    te::Clip* copyClipTo (te::Clip& src, te::AudioTrack& dest, te::TimePosition start)
    {
        if (auto* mc = dynamic_cast<te::MidiClip*> (&src))
            return copyMidiClip (*mc, dest, start);

        if (auto* ac = dynamic_cast<te::WaveAudioClip*> (&src))
        {
            auto pos = ac->getPosition();
            te::ClipPosition np;
            np.time   = te::TimeRange (start, start + pos.time.getLength());
            np.offset = pos.offset;

            auto nc = dest.insertWaveClip (ac->getName(), ac->getOriginalFile(), np, false);
            if (nc == nullptr) return nullptr;

            setTempoFollow (*nc, getSourceBpm (*ac), ac->getAutoTempo());
            return nc.get();
        }
        return nullptr;
    }

    juce::StringArray getWaveInputNames (te::Engine& engine)
    {
        juce::StringArray names;
        auto& dm = engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto w = dm.getWaveInDevice (i))
                names.add (w->getName());
        return names;
    }
}
