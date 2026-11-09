#include "Effects.h"

namespace ldaw::fx
{
    static const juce::Identifier fxNameProp { "ldawFxName" };

    juce::String typeName (int type)
    {
        switch (type)
        {
            case reverb:     return "Reverb";
            case delay:      return "Delay";
            case chorus:     return "Chorus";
            case phaser:     return "Phaser";
            case compressor: return "Compressor";
            case maximizer:  return "Maximizer";
            case equaliser:  return "EQ";
            case filter:     return "Filter";
            default:         return {};
        }
    }

    static const char* xmlTypeFor (int type)
    {
        switch (type)
        {
            case reverb:     return te::ReverbPlugin::xmlTypeName;
            case delay:      return te::DelayPlugin::xmlTypeName;
            case chorus:     return te::ChorusPlugin::xmlTypeName;
            case phaser:     return te::PhaserPlugin::xmlTypeName;
            case compressor: return te::CompressorPlugin::xmlTypeName;
            case maximizer:  return te::CompressorPlugin::xmlTypeName;
            case equaliser:  return te::EqualiserPlugin::xmlTypeName;
            case filter:     return te::LowPassPlugin::xmlTypeName;
            default:         return nullptr;
        }
    }

    void setParamNorm (te::Plugin& p, const juce::String& nameContains, float normalised)
    {
        for (int i = 0; i < p.getNumAutomatableParameters(); ++i)
        {
            te::AutomatableParameter::Ptr param (p.getAutomatableParameter (i));
            if (param != nullptr && param->getParameterName().containsIgnoreCase (nameContains))
            {
                param->setNormalisedParameter (juce::jlimit (0.0f, 1.0f, normalised), juce::sendNotification);
                return;
            }
        }
    }

    te::AutomatableParameter* paramByName (te::Plugin* p, const juce::StringArray& keys)
    {
        if (p == nullptr) return nullptr;
        for (auto& k : keys)
            for (int i = 0; i < p->getNumAutomatableParameters(); ++i)
                if (auto a = p->getAutomatableParameter (i))
                    if (a->getParameterName().containsIgnoreCase (k))
                        return a.get();
        return nullptr;
    }

    te::Plugin::Ptr create (te::Edit& edit, int type)
    {
        auto* xml = xmlTypeFor (type);
        if (xml == nullptr) return {};

        auto p = edit.getPluginCache().createNewPlugin (xml, {});
        if (p == nullptr) return {};

        p->state.setProperty (fxNameProp, typeName (type), nullptr);

        if (type == delay)
        {
            if (auto* d = dynamic_cast<te::DelayPlugin*> (p.get()))
                d->lengthMs = (int) std::round (30000.0 / projectBpm (edit));   // 1/8 note
            setParamNorm (*p, "feedback", 0.55f);
            setParamNorm (*p, "mix", 0.3f);
        }
        else if (type == maximizer)
        {
            setParamNorm (*p, "thresh",  0.70f);
            setParamNorm (*p, "ratio",   1.00f);
            setParamNorm (*p, "attack",  0.00f);
            setParamNorm (*p, "release", 0.12f);
            setParamNorm (*p, "output",  0.62f);
        }
        return p;
    }

    juce::String displayName (te::Plugin& p)
    {
        auto n = p.state.getProperty (fxNameProp).toString();
        return n.isNotEmpty() ? n : p.getName();
    }

    bool isInsert (te::Plugin& p)
    {
        return dynamic_cast<te::VolumeAndPanPlugin*> (&p) == nullptr
            && dynamic_cast<te::LevelMeterPlugin*>   (&p) == nullptr
            && dynamic_cast<te::AuxSendPlugin*>      (&p) == nullptr
            && dynamic_cast<te::AuxReturnPlugin*>    (&p) == nullptr
            && ! p.state.hasProperty (channelProp)
            && ! (dynamic_cast<te::ExternalPlugin*>  (&p) != nullptr && p.isSynth());
    }

    int insertIndex (te::PluginList& list)
    {
        int i = 0;
        for (auto p : list.getPlugins())
        {
            if (dynamic_cast<te::VolumeAndPanPlugin*> (&*p) != nullptr
                || dynamic_cast<te::LevelMeterPlugin*> (&*p) != nullptr
                || dynamic_cast<te::AuxSendPlugin*> (&*p) != nullptr)
                return i;
            ++i;
        }
        return i;
    }

    juce::Array<te::Plugin*> getInserts (te::PluginList& list)
    {
        juce::Array<te::Plugin*> out;
        for (auto p : list.getPlugins())
            if (isInsert (*p))
                out.add (&*p);
        return out;
    }

    static int absoluteIndex (te::PluginList& list, te::Plugin* target)
    {
        int i = 0;
        for (auto p : list.getPlugins())
        {
            if (&*p == target) return i;
            ++i;
        }
        return -1;
    }

    void movePlugin (te::PluginList& list, te::Plugin& p, int delta)
    {
        auto inserts = getInserts (list);
        const int from = inserts.indexOf (&p);
        const int to = from + delta;
        if (from < 0 || ! juce::isPositiveAndBelow (to, inserts.size()) || delta == 0) return;

        te::Plugin::Ptr keep (&p);
        te::Plugin::Ptr other (inserts[to]);

        p.edit.getUndoManager().beginNewTransaction();
        keep->removeFromParent();

        const int otherIndex = absoluteIndex (list, other.get());
        if (otherIndex < 0) { list.insertPlugin (keep, insertIndex (list), nullptr); return; }

        list.insertPlugin (keep, delta > 0 ? otherIndex + 1 : otherIndex, nullptr);
    }

    te::Plugin::Ptr addInsert (te::Edit& edit, te::PluginList& list, int type)
    {
        if (getInserts (list).size() >= maxInserts) return {};
        auto p = create (edit, type);
        if (p != nullptr)
        {
            edit.getUndoManager().beginNewTransaction();
            list.insertPlugin (p, insertIndex (list), nullptr);
        }
        return p;
    }

    te::Plugin::Ptr addInsert (te::Edit& edit, te::PluginList& list, const juce::PluginDescription& desc)
    {
        if (getInserts (list).size() >= maxInserts) return {};
        auto p = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);
        if (p != nullptr)
        {
            edit.getUndoManager().beginNewTransaction();
            list.insertPlugin (p, insertIndex (list), nullptr);
        }
        return p;
    }

    void makeFullyWet (te::Plugin& p)
    {
        if (dynamic_cast<te::ReverbPlugin*> (&p) != nullptr)
        {
            setParamNorm (p, "dry", 0.0f);
            setParamNorm (p, "wet", 0.8f);
        }
        else
        {
            setParamNorm (p, "mix", 1.0f);
        }
    }

    //--------------------------------------------------------------------------
    te::Plugin* findChannelPlugin (te::PluginList& list, const juce::String& which)
    {
        for (auto p : list.getPlugins())
            if (p->state.getProperty (channelProp).toString() == which)
                return &*p;
        return nullptr;
    }

    te::Plugin* ensureChannelPlugin (te::Edit& edit, te::PluginList& list, const juce::String& which)
    {
        if (auto* existing = findChannelPlugin (list, which))
            return existing;

        auto p = edit.getPluginCache().createNewPlugin (which == "eq" ? te::EqualiserPlugin::xmlTypeName
                                                                      : te::CompressorPlugin::xmlTypeName, {});
        if (p == nullptr) return nullptr;

        p->state.setProperty (channelProp, which, nullptr);
        p->state.setProperty (fxNameProp, which == "eq" ? "Channel EQ" : "Channel Dynamics", nullptr);

        int index = 0, i = 0;
        for (auto existing : list.getPlugins())
        {
            ++i;
            const bool isSynth = dynamic_cast<te::ExternalPlugin*> (&*existing) != nullptr && existing->isSynth();
            const bool isReturn = dynamic_cast<te::AuxReturnPlugin*> (&*existing) != nullptr;
            const bool isDyn = existing->state.getProperty (channelProp).toString() == "dyn";
            if (isSynth || isReturn || (which == "eq" && isDyn))
                index = i;
        }

        list.insertPlugin (p, index, nullptr);
        return p.get();
    }

    te::AutomatableParameter* eqParam (te::Plugin* p, int band, bool freq)
    {
        if (p == nullptr) return nullptr;

        juce::Array<te::AutomatableParameter*> matches;
        for (int i = 0; i < p->getNumAutomatableParameters(); ++i)
            if (auto a = p->getAutomatableParameter (i))
            {
                const auto n = a->getParameterName().toLowerCase();
                if (freq ? n.contains ("freq") : n.contains ("gain"))
                    matches.add (a.get());
            }

        for (auto* a : matches)
        {
            const auto n = a->getParameterName().toLowerCase();
            int b = -1;
            if (n.contains ("mid"))      b = n.contains ("2") ? 2 : 1;
            else if (n.contains ("lo"))  b = 0;
            else if (n.contains ("hi"))  b = 3;
            if (b == band) return a;
        }
        return juce::isPositiveAndBelow (band, matches.size()) ? matches[band] : nullptr;
    }

    //--------------------------------------------------------------------------
    te::AuxSendPlugin* findSend (te::AudioTrack& t, int bus)
    {
        for (auto p : t.pluginList.getPlugins())
            if (auto* s = dynamic_cast<te::AuxSendPlugin*> (&*p))
                if (s->busNumber.get() == bus)
                    return s;
        return nullptr;
    }

    float getSendDb (te::AudioTrack& t, int bus)
    {
        if (auto* s = findSend (t, bus))
            return s->getGainDb();
        return -60.0f;
    }

    void setSendDb (te::Edit& edit, te::AudioTrack& t, int bus, float db)
    {
        auto* s = findSend (t, bus);

        if (db <= -59.9f)
        {
            if (s != nullptr)
                s->deleteFromParent();
            return;
        }

        if (s == nullptr)
        {
            auto p = edit.getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
            s = dynamic_cast<te::AuxSendPlugin*> (p.get());
            if (s == nullptr) return;
            s->busNumber = bus;
            t.pluginList.insertPlugin (p, t.pluginList.getPlugins().size(), nullptr);
        }

        s->setGainDb (db);
    }

    //--------------------------------------------------------------------------
    // Send presets
    //--------------------------------------------------------------------------
    struct SendPreset { const char* name; int type; double amount; };

    static const SendPreset sendPresets[] =
    {
        { "Reverb Room",      reverb, 0.35 },
        { "Reverb Hall",      reverb, 0.80 },
        { "Reverb Plate",     reverb, 0.55 },
        { "Delay 1/4",        delay,  1.00 },
        { "Delay 1/8",        delay,  0.50 },
        { "Delay 1/8 dotted", delay,  0.75 },
        { "Delay 1/16",       delay,  0.25 },
        { "Chorus",           chorus, 0.0 },
        { "Phaser",           phaser, 0.0 },
        { "Filter",           filter, 0.0 },
    };

    int numSendPresets()                { return (int) (sizeof (sendPresets) / sizeof (sendPresets[0])); }
    juce::String sendPresetName (int i) { return juce::isPositiveAndBelow (i, numSendPresets()) ? juce::String (sendPresets[i].name) : juce::String(); }

    te::Plugin::Ptr createSendPreset (te::Edit& edit, int i)
    {
        if (! juce::isPositiveAndBelow (i, numSendPresets())) return {};
        const auto& pr = sendPresets[i];

        auto p = create (edit, pr.type);
        if (p == nullptr) return {};

        p->state.setProperty (fxNameProp, juce::String (pr.name), nullptr);

        if (pr.type == reverb)
        {
            setParamNorm (*p, "room",  (float) pr.amount);
            setParamNorm (*p, "damp",  i == 2 ? 0.2f : 0.5f);
            setParamNorm (*p, "width", 1.0f);
        }
        else if (pr.type == delay)
        {
            if (auto* d = dynamic_cast<te::DelayPlugin*> (p.get()))
                d->lengthMs = (int) std::round (pr.amount * 60000.0 / projectBpm (edit));
            setParamNorm (*p, "feedback", 0.4f);
        }

        makeFullyWet (*p);
        return p;
    }

    te::AudioTrack* returnTrack (te::Edit& edit, int bus)
    {
        for (auto* t : te::getAudioTracks (edit))
            if (isReturnTrack (*t) && returnBusOf (*t) == bus)
                return t;
        return nullptr;
    }

    juce::String returnEffectName (te::Edit& edit, int bus)
    {
        if (auto* t = returnTrack (edit, bus))
        {
            auto inserts = getInserts (t->pluginList);
            if (! inserts.isEmpty())
                return displayName (*inserts.getFirst());
        }
        return "(empty)";
    }

    void setReturnEffect (te::Edit& edit, int bus, int presetIndex)
    {
        auto* t = returnTrack (edit, bus);
        if (t == nullptr) return;

        edit.getUndoManager().beginNewTransaction();

        for (auto* p : getInserts (t->pluginList))
            p->deleteFromParent();

        if (auto np = createSendPreset (edit, presetIndex))
            t->pluginList.insertPlugin (np, insertIndex (t->pluginList), nullptr);

        t->setName ("FX " + juce::String (bus + 1) + "  " + sendPresetName (presetIndex));
    }

    void ensureReturnTracks (te::Edit& edit)
    {
        static const int defaults[numSendBuses] = { 1, 5, 7, 8 };   // Hall, 1/8 dotted, Chorus, Phaser

        for (int bus = 0; bus < numSendBuses; ++bus)
        {
            if (returnTrack (edit, bus) != nullptr) continue;

            const int count = te::getAudioTracks (edit).size();
            edit.ensureNumberOfAudioTracks (count + 1);
            auto* t = te::getAudioTracks (edit).getLast();
            if (t == nullptr) continue;

            t->state.setProperty (returnBusProp, bus, nullptr);
            t->setName ("FX " + juce::String (bus + 1) + "  " + sendPresetName (defaults[bus]));

            auto ret = edit.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
            if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (ret.get()))
            {
                r->busNumber = bus;
                t->pluginList.insertPlugin (ret, 0, nullptr);
            }

            if (auto effect = createSendPreset (edit, defaults[bus]))
                t->pluginList.insertPlugin (effect, insertIndex (t->pluginList), nullptr);
        }
    }

    te::LevelMeterPlugin* ensureMasterMeter (te::Edit& edit)
    {
        auto& list = edit.getMasterPluginList();
        for (auto p : list.getPlugins())
            if (auto* m = dynamic_cast<te::LevelMeterPlugin*> (&*p))
                return m;

        auto p = edit.getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {});
        if (p == nullptr) return nullptr;
        list.insertPlugin (p, list.getPlugins().size(), nullptr);
        return dynamic_cast<te::LevelMeterPlugin*> (p.get());
    }

    void openWindow (te::Plugin& p)
    {
        if (p.windowState != nullptr)
            p.windowState->showWindowExplicitly();
    }

    void showAddEffectMenu (te::Edit& edit, te::PluginList& list, juce::Component* target, std::function<void()> onDone)
    {
        juce::PopupMenu m;
        const bool full = getInserts (list).size() >= maxInserts;
        if (full) m.addItem (-1, "All " + juce::String (maxInserts) + " slots are used", false);

        for (int t = reverb; t < numTypes; ++t)
            m.addItem (t, typeName (t), ! full);

        juce::Array<juce::PluginDescription> vsts;
        for (auto& d : edit.engine.getPluginManager().knownPluginList.getTypes())
            if (! d.isInstrument)
                vsts.add (d);

        if (! vsts.isEmpty())
        {
            juce::PopupMenu sub;
            for (int i = 0; i < vsts.size(); ++i)
                sub.addItem (1000 + i, vsts[i].name + "  (" + vsts[i].manufacturerName + ")", ! full);
            m.addSeparator();
            m.addSubMenu ("VST3 effects", sub);
        }

        te::Edit* e = &edit;
        te::PluginList* l = &list;

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                         [e, l, vsts, onDone] (int r)
        {
            te::Plugin::Ptr p;
            if (r >= reverb && r < numTypes)               p = addInsert (*e, *l, r);
            else if (r >= 1000 && r - 1000 < vsts.size())  p = addInsert (*e, *l, vsts[r - 1000]);

            // VST3 effects open their own window; built-in ones are edited in the FX rack
            if (p != nullptr && dynamic_cast<te::ExternalPlugin*> (p.get()) != nullptr)
                openWindow (*p);
            if (onDone) onDone();
        });
    }
}
