#pragma once
// L-DAW 0.5 effects: inserts, send presets, FX rack helpers
#include "Theme.h"

namespace ldaw::fx
{
    enum Type
    {
        reverb = 1, delay, chorus, phaser, compressor, maximizer, equaliser, filter, numTypes
    };

    constexpr int maxInserts = 8;

    juce::String typeName (int type);
    te::Plugin::Ptr create (te::Edit&, int type);
    juce::String displayName (te::Plugin&);

    bool isInsert (te::Plugin&);
    int  insertIndex (te::PluginList&);

    te::Plugin::Ptr addInsert (te::Edit&, te::PluginList&, int type);
    te::Plugin::Ptr addInsert (te::Edit&, te::PluginList&, const juce::PluginDescription&);
    juce::Array<te::Plugin*> getInserts (te::PluginList&);
    void movePlugin (te::PluginList&, te::Plugin&, int delta);   // reorder among inserts

    void setParamNorm (te::Plugin&, const juce::String& nameContains, float normalised);
    te::AutomatableParameter* paramByName (te::Plugin*, const juce::StringArray& nameContains);
    void makeFullyWet (te::Plugin&);

    // Channel-strip dynamics ("dyn") and EQ ("eq")
    te::Plugin* findChannelPlugin (te::PluginList&, const juce::String& which);
    te::Plugin* ensureChannelPlugin (te::Edit&, te::PluginList&, const juce::String& which);
    te::AutomatableParameter* eqParam (te::Plugin*, int band, bool freq);

    // Sends
    te::AuxSendPlugin* findSend (te::AudioTrack&, int bus);
    float getSendDb (te::AudioTrack&, int bus);
    void  setSendDb (te::Edit&, te::AudioTrack&, int bus, float db);

    // Send effects (one return channel per bus, Reason-style)
    int  numSendPresets();
    juce::String sendPresetName (int index);
    te::Plugin::Ptr createSendPreset (te::Edit&, int index);
    te::AudioTrack* returnTrack (te::Edit&, int bus);
    juce::String returnEffectName (te::Edit&, int bus);
    void setReturnEffect (te::Edit&, int bus, int presetIndex);

    inline juce::Colour sendColour (int bus)
    {
        static const juce::uint32 c[] = { 0xff36b3a8, 0xff5b8def, 0xffb36ad6, 0xffe0c341 };
        return juce::Colour (c[(size_t) (((bus % 4) + 4) % 4)]);
    }

    void ensureReturnTracks (te::Edit&);
    te::LevelMeterPlugin* ensureMasterMeter (te::Edit&);

    void openWindow (te::Plugin&);
    void showAddEffectMenu (te::Edit&, te::PluginList&, juce::Component* target, std::function<void()> onDone);
}
