#pragma once
// L-DAW 0.6
#include <JuceHeader.h>
#include <optional>
#include <type_traits>

#define LDAW_VERSION_STRING "0.6.0"

namespace te = tracktion;

namespace ldaw
{
    constexpr int rowHeight      = 58;
    constexpr int autoLaneHeight = 46;
    constexpr int rulerHeight    = 26;
    constexpr int headerWidth    = 560;

    namespace colours
    {
        const juce::Colour background  { 0xff1b1c1f };
        const juce::Colour panel       { 0xff26272b };
        const juce::Colour panelLight  { 0xff303137 };
        const juce::Colour accent      { 0xffe8752a };   // vintage orange
        const juce::Colour clip        { 0xff3f8fd2 };
        const juce::Colour audioClip   { 0xff4fae5c };
        const juce::Colour automation  { 0xfff2a33a };
        const juce::Colour text        { 0xffe6e6e6 };
        const juce::Colour dimText     { 0xff9a9ca3 };
        const juce::Colour record      { 0xffd8383a };
        const juce::Colour play        { 0xff3fbf6a };
        const juce::Colour send        { 0xff36b3a8 };
        const juce::Colour selection   { 0xfff2d16b };
        const juce::Colour bus         { 0xff8a7fd6 };
        const juce::Colour marker      { 0xff5fc8ff };
    }

    inline juce::Colour trackColour (int index)
    {
        static const juce::uint32 palette[] = { 0xffe8752a, 0xff3f8fd2, 0xff58b85c, 0xffd84a6b,
                                                0xffb36ad6, 0xffe0c341, 0xff36b3a8, 0xff8d9bb0 };
        return juce::Colour (palette[(size_t) (((index % 8) + 8) % 8)]);
    }

    //--------------------------------------------------------------------------
    // Track / clip / edit properties stored in the project
    //--------------------------------------------------------------------------
    inline const juce::Identifier returnBusProp { "ldawReturnBus" };
    inline const juce::Identifier busProp       { "ldawBus" };
    inline const juce::Identifier kindProp      { "ldawKind" };
    inline const juce::Identifier inputProp     { "ldawInput" };
    inline const juce::Identifier autoLanesProp { "ldawAutoLanes" };
    inline const juce::Identifier sourceBpmProp { "ldawSourceBpm" };
    inline const juce::Identifier channelProp   { "ldawChannel" };
    inline const juce::Identifier markersProp   { "ldawMarkers" };
    inline const juce::Identifier countInProp   { "ldawCountIn" };
    constexpr int numSendBuses = 4;

    inline bool isReturnTrack (const te::Track& t)  { return t.state.hasProperty (returnBusProp); }
    inline int  returnBusOf (const te::Track& t)     { return (int) t.state.getProperty (returnBusProp, -1); }
    inline bool isLdawBus (const te::Track& t)     { return t.state.hasProperty (busProp); }

    // "Audio track" in L-DAW terms (record/import audio, no instrument).
    inline bool isAudioTrack (const te::Track& t)      { return t.state.getProperty (kindProp).toString() == "audio"; }
    inline bool isAudioTrack (const te::AudioTrack& t) { return t.state.getProperty (kindProp).toString() == "audio"; }

    // Tracks shown in the arrange view (instrument + audio tracks)
    inline juce::Array<te::AudioTrack*> getMusicTracks (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto* t : te::getAudioTracks (e))
            if (! isReturnTrack (*t) && ! isLdawBus (*t))
                out.add (t);
        return out;
    }

    // Group / bus channels (mixer only)
    inline juce::Array<te::AudioTrack*> getLdawBuses (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto* t : te::getAudioTracks (e))
            if (isLdawBus (*t))
                out.add (t);
        return out;
    }

    inline juce::Array<te::AudioTrack*> getReturnTracks (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (int bus = 0; bus < numSendBuses; ++bus)
            for (auto* t : te::getAudioTracks (e))
                if (isReturnTrack (*t) && returnBusOf (*t) == bus)
                    out.add (t);
        return out;
    }

    //--------------------------------------------------------------------------
    // AutomationCurve helpers: work with both the older (no UndoManager) and the
    // newer Tracktion Engine signatures - the compiler picks whichever exists.
    //--------------------------------------------------------------------------
    namespace curve
    {
        template <typename C>
        float valueAt (C& c, te::TimePosition t, float def)
        {
            if constexpr (requires { c.getValueAt (t, def); }) return c.getValueAt (t, def);
            else                                                return c.getNumPoints() > 0 ? c.getValueAt (t) : def;
        }

        template <typename C>
        int addPoint (C& c, te::TimePosition t, float v, juce::UndoManager* um)
        {
            if constexpr (requires { c.addPoint (t, v, 0.0f, um); }) return (int) c.addPoint (t, v, 0.0f, um);
            else                                                      return (int) c.addPoint (t, v, 0.0f);
        }

        template <typename C>
        void removePoint (C& c, int i, juce::UndoManager* um)
        {
            if constexpr (requires { c.removePoint (i, um); }) c.removePoint (i, um);
            else                                                c.removePoint (i);
        }

        template <typename C>
        void clear (C& c, juce::UndoManager* um)
        {
            if constexpr (requires { c.clear (um); }) c.clear (um);
            else                                       c.clear();
        }

        template <typename F>
        int asIndex (int fallback, F&& call)
        {
            using R = decltype (call());
            if constexpr (std::is_void_v<R>) { call(); return fallback; }
            else                             return (int) call();
        }

        template <typename C>
        int movePoint (C& c, int i, te::TimePosition t, float v, juce::UndoManager* um)
        {
            if constexpr (requires { c.movePoint (i, t, v, std::nullopt, false, um); })
                return asIndex (i, [&] { return c.movePoint (i, t, v, std::nullopt, false, um); });
            else if constexpr (requires { c.movePoint (i, t, v, false, um); })
                return asIndex (i, [&] { return c.movePoint (i, t, v, false, um); });
            else
                return asIndex (i, [&] { return c.movePoint (i, t, v, false); });
        }
    }

    //--------------------------------------------------------------------------
    inline double beatOf (te::Edit& e, te::TimePosition t)  { return e.tempoSequence.toBeats (t).inBeats(); }
    inline te::TimePosition timeOf (te::Edit& e, double b)  { return e.tempoSequence.toTime (te::BeatPosition::fromBeats (b)); }

    inline double projectBpm (te::Edit& e)
    {
        if (auto t = e.tempoSequence.getTempo (0))
            return t->getBpm();
        return 120.0;
    }

    inline juce::String beatToBarText (double beat)
    {
        const int bar = (int) std::floor (beat / 4.0) + 1;
        const int b   = (int) std::floor (std::fmod (juce::jmax (0.0, beat), 4.0)) + 1;
        return juce::String (bar) + "." + juce::String (b);
    }

    inline te::MidiClip* findMidiClip (te::Edit* e, te::EditItemID id)
    {
        if (e == nullptr || ! id.isValid()) return nullptr;
        return dynamic_cast<te::MidiClip*> (te::findClipForID (*e, id));
    }

    inline te::MidiClip* copyMidiClip (te::MidiClip& src, te::AudioTrack& dest, te::TimePosition start)
    {
        auto& e = src.edit;
        const double startBeat = beatOf (e, start);
        const double lenBeats  = beatOf (e, src.getPosition().getEnd()) - beatOf (e, src.getPosition().getStart());

        auto nc = dest.insertMIDIClip (te::TimeRange (start, timeOf (e, startBeat + lenBeats)), nullptr);
        if (nc == nullptr) return nullptr;

        nc->setName (src.getName());
        nc->getSequence().copyFrom (src.getSequence(), &e.getUndoManager());
        return nc.get();
    }

    //--------------------------------------------------------------------------
    // Shared note clipboard (piano roll <-> step editor)
    //--------------------------------------------------------------------------
    struct ClipNote { int pitch; double start, length; int velocity; };

    inline std::vector<ClipNote>& noteClipboard()  { static std::vector<ClipNote> v; return v; }
    inline double& noteClipboardSpan()            { static double s = 0.0; return s; }

    //--------------------------------------------------------------------------
    // Markers: stored in the edit as "beat|name;beat|name"
    //--------------------------------------------------------------------------
    struct Marker { double beat; juce::String name; };

    inline std::vector<Marker> getMarkers (te::Edit& e)
    {
        std::vector<Marker> out;
        for (auto& item : juce::StringArray::fromTokens (e.state.getProperty (markersProp).toString(), ";", ""))
            if (item.contains ("|"))
                out.push_back ({ item.upToFirstOccurrenceOf ("|", false, false).getDoubleValue(),
                                 item.fromFirstOccurrenceOf ("|", false, false) });
        std::sort (out.begin(), out.end(), [] (const Marker& a, const Marker& b) { return a.beat < b.beat; });
        return out;
    }

    inline void setMarkers (te::Edit& e, const std::vector<Marker>& markers)
    {
        juce::StringArray items;
        for (auto& m : markers)
            items.add (juce::String (m.beat, 3) + "|" + m.name.removeCharacters (";|"));
        e.state.setProperty (markersProp, items.joinIntoString (";"), &e.getUndoManager());
    }

    //--------------------------------------------------------------------------
    // Automation lanes: stored per track as "pluginID|paramID;pluginID|paramID"
    //--------------------------------------------------------------------------
    inline juce::String paramKey (te::Plugin& p, te::AutomatableParameter& a)
    {
        return p.itemID.toString() + "|" + a.paramID;
    }

    inline te::Plugin* findPluginByID (te::AudioTrack& t, const juce::String& pluginID)
    {
        for (auto p : t.pluginList.getPlugins())
            if (p->itemID.toString() == pluginID)
                return &*p;
        return nullptr;
    }

    inline te::AutomatableParameter* findParam (te::AudioTrack& t, const juce::String& key)
    {
        if (auto* p = findPluginByID (t, key.upToFirstOccurrenceOf ("|", false, false)))
        {
            const auto id = key.fromFirstOccurrenceOf ("|", false, false);
            for (int i = 0; i < p->getNumAutomatableParameters(); ++i)
                if (auto a = p->getAutomatableParameter (i))
                    if (a->paramID == id)
                        return a.get();
        }
        return nullptr;
    }

    inline juce::StringArray getAutoLanes (const te::Track& t)
    {
        auto s = juce::StringArray::fromTokens (t.state.getProperty (autoLanesProp).toString(), ";", "");
        s.removeEmptyStrings();
        return s;
    }

    inline void setAutoLanes (te::Track& t, const juce::StringArray& keys)
    {
        t.state.setProperty (autoLanesProp, keys.joinIntoString (";"), nullptr);
    }

    struct Lane
    {
        te::AudioTrack* track = nullptr;
        int trackIndex = 0;
        te::AutomatableParameter::Ptr param;
        juce::String key;
        int y = 0, h = 0;

        bool isAuto() const { return param != nullptr; }
    };
}
