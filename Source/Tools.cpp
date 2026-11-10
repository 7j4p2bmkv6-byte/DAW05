#include "Tools.h"

namespace ldaw::routing
{
    te::AudioTrack* outputBus (te::AudioTrack& t)
    {
        return t.getOutput().getDestinationTrack();
    }

    void setOutput (te::AudioTrack& t, te::AudioTrack* bus)
    {
        if (bus == &t) return;
        if (bus != nullptr) t.getOutput().setOutputToTrack (bus);
        else                t.getOutput().setOutputToDefaultDevice (false);
    }

    juce::String outputName (te::AudioTrack& t)
    {
        if (auto* b = outputBus (t))
            return b->getName();
        return "Master";
    }

    te::AudioTrack* createBus (te::Edit& edit, const juce::String& name)
    {
        // place after the last music/bus track, before the FX returns
        te::Track* preceding = nullptr;
        for (auto* t : te::getAudioTracks (edit))
            if (! isReturnTrack (*t))
                preceding = t;

        auto t = edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, preceding), nullptr);
        if (t == nullptr) return nullptr;

        t->state.setProperty (busProp, true, nullptr);
        t->setName (name);
        return t.get();
    }

    void deleteBus (te::Edit& edit, te::AudioTrack& bus)
    {
        edit.getUndoManager().beginNewTransaction();

        for (auto* t : te::getAudioTracks (edit))
            if (t != &bus && outputBus (*t) == &bus)
                setOutput (*t, nullptr);

        edit.deleteTrack (&bus);
    }

    void showOutputMenu (te::Edit& edit, te::AudioTrack& track, juce::Component* target, std::function<void()> onDone)
    {
        auto buses = getLdawBuses (edit);
        auto* current = outputBus (track);

        juce::PopupMenu m;
        m.addSectionHeader ("Output of " + track.getName());
        m.addItem (1, "Master", true, current == nullptr);
        for (int i = 0; i < buses.size(); ++i)
            if (buses[i] != &track)
                m.addItem (100 + i, buses[i]->getName(), true, buses[i] == current);
        m.addSeparator();
        m.addItem (2, "New bus...");

        te::Edit* e = &edit;
        const auto trackID = track.itemID;

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                         [e, trackID, buses, onDone] (int r)
        {
            if (r == 0) return;

            te::AudioTrack* t = nullptr;
            for (auto* a : te::getAudioTracks (*e))
                if (a->itemID == trackID) t = a;
            if (t == nullptr) return;

            e->getUndoManager().beginNewTransaction();

            if (r == 1)
                setOutput (*t, nullptr);
            else if (r >= 100 && r - 100 < buses.size())
                setOutput (*t, buses[r - 100]);
            else if (r == 2)
                if (auto* b = createBus (*e, "Bus " + juce::String (getLdawBuses (*e).size() + 1)))
                    setOutput (*t, b);

            if (onDone) onDone();
        });
    }
}

namespace ldaw::tools
{
    template <typename E>
    static void applyCountIn (E& e, bool on)
    {
        if constexpr (requires { e.setCountInMode (E::CountIn::oneBar); })
            e.setCountInMode (on ? E::CountIn::oneBar : E::CountIn::none);
    }

    void setCountIn (te::Edit& edit, bool on)
    {
        edit.state.setProperty (countInProp, on, nullptr);
        applyCountIn (edit, on);
    }

    bool getCountIn (te::Edit& edit)
    {
        return (bool) edit.state.getProperty (countInProp, false);
    }

    static double songEndBeats (te::Edit& edit)
    {
        double end = 0.0;
        for (auto* t : te::getAudioTracks (edit))
            for (auto* c : t->getClips())
                end = juce::jmax (end, beatOf (edit, c->getPosition().getEnd()));
        return end;
    }

    void showExportDialog (te::Edit& edit, std::function<void (const juce::String&)> status)
    {
        const double endBeat = songEndBeats (edit);
        auto& tc = edit.getTransport();
        const auto loop = tc.getLoopRange();

        auto* aw = new juce::AlertWindow ("Export audio",
                                          "Renders the mix (all tracks, effects and sends) to a WAV file.\n"
                                          "Song length: " + juce::String (endBeat / 4.0, 1) + " bars.",
                                          juce::MessageBoxIconType::NoIcon);
        aw->addComboBox ("range", { "Whole song (start to last clip)", "Loop range only" }, "What to export:");
        aw->addComboBox ("tail",  { "Add 2 s tail (reverb / delay ring-out)", "No tail" }, "Ending:");
        aw->addButton ("Export...", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        te::Edit* e = &edit;
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([e, aw, endBeat, loop, status] (int result)
        {
            if (result != 1) return;

            const bool useLoop = aw->getComboBoxComponent ("range")->getSelectedItemIndex() == 1;
            const bool addTail = aw->getComboBoxComponent ("tail")->getSelectedItemIndex() == 0;

            te::TimeRange range = useLoop ? loop
                                          : te::TimeRange (te::TimePosition(), timeOf (*e, juce::jmax (4.0, endBeat)));
            if (addTail)
                range = te::TimeRange (range.getStart(), range.getEnd() + te::TimeDuration::fromSeconds (2.0));

            auto defaultFile = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getNonexistentChildFile ("L-DAW mix", ".wav");

            auto chooser = std::make_shared<juce::FileChooser> ("Export WAV", defaultFile, "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [e, range, status, chooser] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File()) return;
                file = file.withFileExtension ("wav");
                file.deleteFile();

                e->getTransport().stop (false, true);
                if (status) status ("Exporting " + file.getFileName() + " ...");

                juce::BigInteger tracks;
                const int numTracks = te::getAllTracks (*e).size();
                tracks.setRange (0, numTracks, true);

                const bool ok = te::Renderer::renderToFile ("Export", file, *e, range, tracks, true, false, {}, false);

                if (status)
                    status (ok && file.existsAsFile() ? "Exported " + file.getFullPathName()
                                                      : "Export failed - check that the folder is writable.");

                e->getTransport().ensureContextAllocated();
            });
        }), true);
    }
}
