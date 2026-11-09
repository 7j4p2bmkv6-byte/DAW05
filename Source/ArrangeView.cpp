#include "ArrangeView.h"

using namespace ldaw;

ArrangeView::ArrangeView()
{
    setWantsKeyboardFocus (false);
    formatManager.registerBasicFormats();
}

double ArrangeView::pixelsPerBeat() const     { return juce::jmax (1, getWidth()) / (double) juce::jmax (1, barsVisible * 4); }
double ArrangeView::xToBeats (float x) const  { return juce::jmax (0.0, x / pixelsPerBeat()); }
float  ArrangeView::beatsToX (double b) const { return (float) (b * pixelsPerBeat()); }

int ArrangeView::numTracks() const
{
    int n = 0;
    for (auto& l : lanes) if (! l.isAuto()) ++n;
    return n;
}

int ArrangeView::laneIndexAt (int y) const
{
    for (int i = 0; i < (int) lanes.size(); ++i)
        if (y >= lanes[(size_t) i].y && y < lanes[(size_t) i].y + lanes[(size_t) i].h)
            return i;
    return -1;
}

int ArrangeView::trackIndexAt (int y) const
{
    const int li = laneIndexAt (y);
    if (li >= 0) return lanes[(size_t) li].trackIndex;
    return y < rulerHeight ? 0 : juce::jmax (0, numTracks() - 1);
}

int ArrangeView::laneYForTrack (int trackIndex) const
{
    for (auto& l : lanes)
        if (! l.isAuto() && l.trackIndex == trackIndex)
            return l.y;
    return rulerHeight;
}

void ArrangeView::select (te::EditItemID id)
{
    selectedClip = id;
    if (onClipSelected) onClipSelected (id);
    repaint();
}

juce::AudioThumbnail* ArrangeView::getThumb (const juce::File& f)
{
    const auto key = f.getFullPathName();
    auto it = thumbs.find (key);
    if (it != thumbs.end()) return it->second.get();

    auto t = std::make_unique<juce::AudioThumbnail> (512, formatManager, thumbCache);
    t->setSource (new juce::FileInputSource (f));
    auto* raw = t.get();
    thumbs[key] = std::move (t);
    return raw;
}

//==============================================================================
float ArrangeView::valueToY (const Lane& l, float v) const
{
    auto range = l.param->getValueRange();
    const float norm = range.getLength() > 0.0f ? (v - range.getStart()) / range.getLength() : 0.5f;
    return (float) l.y + 4.0f + (1.0f - juce::jlimit (0.0f, 1.0f, norm)) * (float) (l.h - 8);
}

float ArrangeView::yToValue (const Lane& l, float y) const
{
    auto range = l.param->getValueRange();
    const float norm = 1.0f - juce::jlimit (0.0f, 1.0f, (y - (float) l.y - 4.0f) / (float) juce::jmax (1, l.h - 8));
    return range.getStart() + norm * range.getLength();
}

int ArrangeView::pointNear (const Lane& l, juce::Point<float> p) const
{
    auto& c = l.param->getCurve();
    for (int i = 0; i < c.getNumPoints(); ++i)
    {
        juce::Point<float> pt (beatsToX (beatOf (*edit, c.getPointTime (i))), valueToY (l, c.getPointValue (i)));
        if (pt.getDistanceFrom (p) < 7.0f)
            return i;
    }
    return -1;
}

void ArrangeView::drawAutomationLane (juce::Graphics& g, const Lane& l)
{
    const float w = (float) getWidth();
    g.setColour (colours::background.darker (0.35f));
    g.fillRect (0.0f, (float) l.y, w, (float) l.h);
    g.setColour (colours::automation.withAlpha (0.15f));
    g.drawHorizontalLine (l.y + l.h - 1, 0.0f, w);

    auto& c = l.param->getCurve();
    const float current = l.param->getCurrentValue();

    juce::Path path;
    if (c.getNumPoints() == 0)
    {
        const float y = valueToY (l, current);
        path.startNewSubPath (0.0f, y);
        path.lineTo (w, y);
        g.setColour (colours::automation.withAlpha (0.35f));
        g.strokePath (path, juce::PathStrokeType (1.5f));
        g.setColour (colours::dimText);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("Click to add points, or turn on A.Write and move the control while playing",
                    juce::Rectangle<int> (8, l.y, getWidth() - 16, l.h), juce::Justification::centredLeft);
        return;
    }

    bool first = true;
    for (float x = 0.0f; x <= w; x += 2.0f)
    {
        const float v = ldaw::curve::valueAt (c, timeOf (*edit, xToBeats (x)), current);
        const float y = valueToY (l, v);
        if (first) { path.startNewSubPath (x, y); first = false; }
        else         path.lineTo (x, y);
    }

    juce::Path fill (path);
    fill.lineTo (w, (float) (l.y + l.h));
    fill.lineTo (0.0f, (float) (l.y + l.h));
    fill.closeSubPath();
    g.setColour (colours::automation.withAlpha (0.12f));
    g.fillPath (fill);

    g.setColour (colours::automation);
    g.strokePath (path, juce::PathStrokeType (1.8f));

    for (int i = 0; i < c.getNumPoints(); ++i)
    {
        const float x = beatsToX (beatOf (*edit, c.getPointTime (i)));
        if (x < -6.0f || x > w + 6.0f) continue;
        const float y = valueToY (l, c.getPointValue (i));
        g.setColour (colours::background);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        g.setColour (colours::automation);
        g.drawEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.5f);
    }
}

void ArrangeView::drawAudioClip (juce::Graphics& g, te::WaveAudioClip& clip, juce::Rectangle<float> r)
{
    auto file = clip.getOriginalFile();
    auto* thumb = getThumb (file);
    if (thumb == nullptr || thumb->getTotalLength() <= 0.0) return;

    auto pos = clip.getPosition();
    const double clipSeconds = pos.time.getLength().inSeconds();
    const double srcBpm = audio::getSourceBpm (clip);

    double ratio = clip.getSpeedRatio();
    if (clip.getAutoTempo() && srcBpm > 0.0)
        ratio = projectBpm (*edit) / srcBpm;

    const double srcStart = juce::jmax (0.0, pos.offset.inSeconds());
    const double srcEnd   = juce::jmin (thumb->getTotalLength(), srcStart + clipSeconds * ratio);

    auto area = r.reduced (2.0f).withTrimmedTop (13.0f).toNearestInt();
    if (srcEnd <= srcStart || area.getWidth() < 2) return;

    const double usedSeconds = (srcEnd - srcStart) / juce::jmax (0.0001, ratio);
    area.setWidth ((int) juce::jmin ((double) area.getWidth(), usedSeconds / clipSeconds * area.getWidth()));

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    thumb->drawChannels (g, area, srcStart, srcEnd, 1.0f);
}

void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    if (edit == nullptr) return;

    auto& tc = edit->getTransport();
    const auto w = (float) getWidth();
    const auto h = (float) getHeight();

    for (auto& l : lanes)
    {
        if (l.isAuto()) continue;
        g.setColour (l.trackIndex == selectedTrack ? colours::panel.brighter (0.06f)
                                                   : (l.trackIndex % 2 == 0 ? colours::background : colours::background.brighter (0.03f)));
        g.fillRect (0.0f, (float) l.y, w, (float) l.h);
    }

    for (auto& l : lanes)
        if (l.isAuto())
            drawAutomationLane (g, l);

    if (tc.looping.get())
    {
        auto lr = tc.getLoopRange();
        const float x1 = beatsToX (beatOf (*edit, lr.getStart()));
        const float x2 = beatsToX (beatOf (*edit, lr.getEnd()));
        g.setColour (colours::accent.withAlpha (0.07f));
        g.fillRect (x1, (float) rulerHeight, x2 - x1, h - rulerHeight);
        g.setColour (colours::accent.withAlpha (0.8f));
        g.fillRect (x1, (float) rulerHeight - 5.0f, x2 - x1, 5.0f);
    }

    for (int beat = 0; beat <= barsVisible * 4; ++beat)
    {
        g.setColour (juce::Colours::white.withAlpha ((beat % 4) == 0 ? 0.14f : 0.05f));
        g.drawVerticalLine ((int) beatsToX (beat), (float) rulerHeight, h);
    }

    g.setColour (colours::panel);
    g.fillRect (0.0f, 0.0f, w, (float) rulerHeight - 5.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    for (int bar = 0; bar < barsVisible; ++bar)
    {
        const float x = beatsToX (bar * 4);
        g.setColour (colours::dimText);
        g.drawVerticalLine ((int) x, 0.0f, (float) rulerHeight - 5.0f);
        g.drawText (juce::String (bar + 1), (int) x + 4, 2, 40, 16, juce::Justification::left);
    }

    for (auto& l : lanes)
    {
        if (l.isAuto()) continue;
        const float laneY = (float) l.y;
        const auto trackCol = trackColour (l.trackIndex);

        for (auto* clip : l.track->getClips())
        {
            auto pos = clip->getPosition();
            const double sb = beatOf (*edit, pos.getStart());
            const double eb = beatOf (*edit, pos.getEnd());
            juce::Rectangle<float> r (beatsToX (sb), laneY + 3.0f,
                                      juce::jmax (3.0f, beatsToX (eb) - beatsToX (sb)), (float) l.h - 6.0f);

            const bool isSel = clip->itemID == selectedClip;
            const bool isDragged = drag.active && drag.moved && ! drag.copy && clip->itemID == drag.clipID;
            auto* wave = dynamic_cast<te::WaveAudioClip*> (clip);
            const auto base = trackCol.withMultipliedSaturation (0.85f).withMultipliedBrightness (0.85f);

            g.setColour (base.withAlpha (isDragged ? 0.35f : 0.80f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (base.darker (0.35f).withAlpha (0.9f));
            g.fillRect (r.withHeight (14.0f).reduced (1.0f, 1.0f));
            g.setColour (isSel ? juce::Colours::white : base.brighter (0.5f));
            g.drawRoundedRectangle (r, 4.0f, isSel ? 2.0f : 1.0f);

            juce::String title = clip->getName();
            if (wave != nullptr && wave->getAutoTempo())
                title << "   [" << juce::String (audio::getSourceBpm (*wave), 1) << " bpm > follows tempo]";

            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (title, r.reduced (4.0f, 1.0f).removeFromTop (13.0f), juce::Justification::left, true);

            if (wave != nullptr)
            {
                drawAudioClip (g, *wave, r);
            }
            else if (auto* mc = dynamic_cast<te::MidiClip*> (clip))
            {
                auto& notes = mc->getSequence().getNotes();
                if (notes.size() > 0)
                {
                    int lo = 127, hi = 0;
                    for (auto* n : notes) { lo = juce::jmin (lo, n->getNoteNumber()); hi = juce::jmax (hi, n->getNoteNumber()); }
                    const int span = juce::jmax (12, hi - lo + 1);
                    auto noteArea = r.reduced (2.0f).withTrimmedTop (13.0f);
                    const float nh = juce::jmax (1.5f, noteArea.getHeight() / (float) span);

                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    for (auto* n : notes)
                    {
                        const double ns = sb + n->getStartBeat().inBeats();
                        if (ns >= eb) continue;
                        const double nl = juce::jmin (n->getLengthBeats().inBeats(), eb - ns);
                        const float ny = noteArea.getBottom() - (float) (n->getNoteNumber() - lo + 1) * nh;
                        g.fillRect (beatsToX (ns), ny, juce::jmax (1.5f, (float) (nl * pixelsPerBeat()) - 1.0f), nh);
                    }
                }
            }

            if (isSel)
            {
                g.setColour (juce::Colours::white);
                g.fillRect (r.getRight() - 4.0f, r.getY() + 4.0f, 3.0f, r.getHeight() - 8.0f);
            }
        }
    }

    if (drag.active && drag.moved)
    {
        const double s = drag.resize ? drag.start0 : drag.newStart;
        const double e = drag.resize ? drag.newEnd : drag.newStart + (drag.end0 - drag.start0);
        const int    t = drag.resize ? drag.track0 : drag.newTrack;
        juce::Rectangle<float> ghost (beatsToX (s), (float) laneYForTrack (t) + 3.0f,
                                      beatsToX (e) - beatsToX (s), (float) rowHeight - 6.0f);
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRoundedRectangle (ghost, 4.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (ghost, 4.0f, 2.0f);
        if (drag.copy)
            g.drawText ("+ copy", ghost.reduced (4.0f), juce::Justification::bottomRight, false);
    }

    const float px = beatsToX (beatOf (*edit, tc.getPosition()));
    g.setColour (tc.isRecording() ? colours::record : juce::Colours::white);
    g.drawVerticalLine ((int) px, 0.0f, h);
}

//==============================================================================
te::Clip* ArrangeView::clipAt (juce::Point<int> p, int& trackIndex) const
{
    trackIndex = -1;
    const int li = laneIndexAt (p.y);
    if (edit == nullptr || li < 0) return nullptr;

    auto& l = lanes[(size_t) li];
    trackIndex = l.trackIndex;
    if (l.isAuto()) return nullptr;

    const double beat = xToBeats ((float) p.x);
    for (auto* clip : l.track->getClips())
    {
        auto pos = clip->getPosition();
        if (beat >= beatOf (*edit, pos.getStart()) && beat < beatOf (*edit, pos.getEnd()))
            return clip;
    }
    return nullptr;
}

void ArrangeView::mouseMove (const juce::MouseEvent& e)
{
    const int li = laneIndexAt (e.y);
    if (li >= 0 && lanes[(size_t) li].isAuto())
    {
        setMouseCursor (pointNear (lanes[(size_t) li], e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor
                                                                         : juce::MouseCursor::CrosshairCursor);
        return;
    }

    int ti = 0;
    if (auto* c = clipAt (e.getPosition(), ti))
    {
        const float right = beatsToX (beatOf (*edit, c->getPosition().getEnd()));
        setMouseCursor (e.x > right - 7 ? juce::MouseCursor::LeftRightResizeCursor
                                        : juce::MouseCursor::DraggingHandCursor);
    }
    else
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void ArrangeView::mouseDown (const juce::MouseEvent& e)
{
    drag = {};
    autoLane = autoPoint = -1;
    if (edit == nullptr) return;

    if (e.y < rulerHeight)
    {
        double beat = xToBeats ((float) e.x);
        if (! e.mods.isShiftDown()) beat = std::floor (beat + 0.5);
        edit->getTransport().setPosition (timeOf (*edit, beat));
        repaint();
        return;
    }

    const int li = laneIndexAt (e.y);

    if (li >= 0 && lanes[(size_t) li].isAuto())
    {
        auto& l = lanes[(size_t) li];
        if (onLaneClicked) onLaneClicked (l.trackIndex);

        auto& c = l.param->getCurve();
        auto* um = &edit->getUndoManager();
        edit->getUndoManager().beginNewTransaction();

        const int near = pointNear (l, e.position);
        if (e.mods.isPopupMenu())
        {
            if (near >= 0) ldaw::curve::removePoint (c, near, um);
            repaint();
            return;
        }

        autoLane = li;
        if (near >= 0)
        {
            autoPoint = near;
        }
        else
        {
            double beat = xToBeats ((float) e.x);
            if (! e.mods.isAltDown()) beat = std::round (beat * 4.0) / 4.0;
            autoPoint = ldaw::curve::addPoint (c, timeOf (*edit, beat), yToValue (l, (float) e.y), um);
        }
        repaint();
        return;
    }

    int trackIndex = 0;
    auto* clip = clipAt (e.getPosition(), trackIndex);

    if (onLaneClicked && trackIndex >= 0)
        onLaneClicked (trackIndex);

    if (clip == nullptr)
    {
        select ({});
        return;
    }

    select (clip->itemID);

    if (e.mods.isPopupMenu())
    {
        showClipMenu (clip->itemID);
        return;
    }

    drag.active   = true;
    drag.clipID   = clip->itemID;
    drag.track0   = drag.newTrack = trackIndex;
    drag.start0   = drag.newStart = beatOf (*edit, clip->getPosition().getStart());
    drag.end0     = drag.newEnd   = beatOf (*edit, clip->getPosition().getEnd());
    drag.downBeat = xToBeats ((float) e.x);
    drag.resize   = e.x > beatsToX (drag.end0) - 7;
}

void ArrangeView::mouseDrag (const juce::MouseEvent& e)
{
    if (autoLane >= 0 && autoPoint >= 0 && autoLane < (int) lanes.size())
    {
        auto& l = lanes[(size_t) autoLane];
        auto& c = l.param->getCurve();
        double beat = xToBeats ((float) e.x);
        if (! e.mods.isAltDown()) beat = std::round (beat * 4.0) / 4.0;
        const float y = juce::jlimit ((float) l.y, (float) (l.y + l.h), (float) e.y);
        autoPoint = ldaw::curve::movePoint (c, autoPoint, timeOf (*edit, beat), yToValue (l, y), &edit->getUndoManager());
        repaint();
        return;
    }

    if (! drag.active) return;

    drag.moved = drag.moved || e.getDistanceFromDragStart() > 3;
    if (! drag.moved) return;

    const double snap  = e.mods.isShiftDown() ? 0.25 : 1.0;
    const double delta = std::round ((xToBeats ((float) e.x) - drag.downBeat) / snap) * snap;

    if (drag.resize)
    {
        drag.newEnd = juce::jmax (drag.start0 + 0.25, drag.end0 + delta);
    }
    else
    {
        drag.newStart = juce::jmax (0.0, drag.start0 + delta);
        drag.newTrack = trackIndexAt (e.y);
        drag.copy     = e.mods.isCtrlDown() || e.mods.isAltDown();
    }
    repaint();
}

void ArrangeView::mouseUp (const juce::MouseEvent&)
{
    if (drag.active && drag.moved)
        applyDrag();

    drag = {};
    autoLane = autoPoint = -1;
    repaint();
}

void ArrangeView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (edit == nullptr || e.y < rulerHeight) return;

    const int li = laneIndexAt (e.y);
    if (li < 0 || lanes[(size_t) li].isAuto()) return;

    int trackIndex = 0;
    if (auto* clip = clipAt (e.getPosition(), trackIndex))
    {
        if (dynamic_cast<te::WaveAudioClip*> (clip) != nullptr)
        {
            if (onClipTempo) onClipTempo (clip->itemID);
        }
        else if (onOpenEditor)
        {
            onOpenEditor (clip->itemID, 0);
        }
        return;
    }

    auto& l = lanes[(size_t) li];
    const double startBeat = std::floor (xToBeats ((float) e.x) / 4.0) * 4.0;

    if (isAudioTrack (*l.track))
    {
        if (onImportAt) onImportAt (l.trackIndex, startBeat);
        return;
    }

    edit->getUndoManager().beginNewTransaction();
    if (auto nc = l.track->insertMIDIClip (te::TimeRange (timeOf (*edit, startBeat), timeOf (*edit, startBeat + 4.0)), nullptr))
    {
        nc->setName ("Clip");
        select (nc->itemID);
        if (onClipsChanged) onClipsChanged();
        if (onOpenEditor) onOpenEditor (nc->itemID, 0);
    }
}

bool ArrangeView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (audio::isAudioFile (juce::File (f)))
            return true;
    return false;
}

void ArrangeView::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::Array<juce::File> list;
    for (auto& f : files)
        if (audio::isAudioFile (juce::File (f)))
            list.add (juce::File (f));

    const int li = laneIndexAt (y);
    const int trackIndex = li >= 0 ? lanes[(size_t) li].trackIndex : -1;
    const double beat = std::floor (xToBeats ((float) x));

    if (onFilesDropped && ! list.isEmpty())
        onFilesDropped (list, trackIndex, beat);
}

//==============================================================================
void ArrangeView::applyDrag()
{
    auto* clip = te::findClipForID (*edit, drag.clipID);
    if (clip == nullptr) return;

    edit->getUndoManager().beginNewTransaction();

    if (drag.resize)
    {
        clip->setEnd (timeOf (*edit, drag.newEnd), true);
    }
    else
    {
        auto tracks = getMusicTracks (*edit);
        auto newStart = timeOf (*edit, drag.newStart);

        if (drag.copy || drag.newTrack != drag.track0)
        {
            if (juce::isPositiveAndBelow (drag.newTrack, tracks.size()))
            {
                if (auto* nc = audio::copyClipTo (*clip, *tracks[drag.newTrack], newStart))
                {
                    if (! drag.copy)
                        clip->removeFromParent();
                    select (nc->itemID);
                }
            }
        }
        else
        {
            clip->setStart (newStart, false, true);
        }
    }

    if (onClipsChanged) onClipsChanged();
}

void ArrangeView::deleteSelectedClip()
{
    if (edit == nullptr) return;
    if (auto* clip = te::findClipForID (*edit, selectedClip))
    {
        edit->getUndoManager().beginNewTransaction();
        clip->removeFromParent();
        select ({});
        if (onClipsChanged) onClipsChanged();
    }
}

void ArrangeView::duplicateSelectedClip()
{
    if (edit == nullptr) return;
    auto* clip = te::findClipForID (*edit, selectedClip);
    if (clip == nullptr) return;

    if (auto* track = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
    {
        edit->getUndoManager().beginNewTransaction();
        if (auto* nc = audio::copyClipTo (*clip, *track, clip->getPosition().getEnd()))
            select (nc->itemID);
        if (onClipsChanged) onClipsChanged();
    }
}

void ArrangeView::showClipMenu (te::EditItemID id)
{
    auto* clip = te::findClipForID (*edit, id);
    if (clip == nullptr) return;
    const bool isAudio = dynamic_cast<te::WaveAudioClip*> (clip) != nullptr;

    juce::PopupMenu m;
    if (isAudio)
    {
        m.addItem (5, "Tempo / time-stretch...");
    }
    else
    {
        m.addItem (1, "Open in piano roll");
        m.addItem (2, "Open in step editor (drums)");
    }
    m.addSeparator();
    m.addItem (3, "Duplicate   (Ctrl+D)");
    m.addItem (4, "Delete      (Del)");

    m.showMenuAsync (juce::PopupMenu::Options(),
                     [safe = juce::Component::SafePointer<ArrangeView> (this), id] (int r)
    {
        if (safe == nullptr) return;
        safe->selectedClip = id;
        if (r == 1 && safe->onOpenEditor) safe->onOpenEditor (id, 1);
        if (r == 2 && safe->onOpenEditor) safe->onOpenEditor (id, 2);
        if (r == 3) safe->duplicateSelectedClip();
        if (r == 4) safe->deleteSelectedClip();
        if (r == 5 && safe->onClipTempo) safe->onClipTempo (id);
    });
}
