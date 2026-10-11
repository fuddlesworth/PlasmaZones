// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The context hold (EE): a window's desktop or activity edit that waits for the
// move that caused it to settle, and its release. The conditions are
// windowcontexthold.h's. The edit handlers that ask for a hold live in
// window_desktop_connections.cpp, and the release points are the drop's exit
// (drag_end.cpp), the move tail's drain (window_moveresize_connections.cpp), the
// output change (window_output_connections.cpp) and the screen-change settle
// (ScreenChangeHandler).

#include "plasmazoneseffect.h"
#include "desktopvisibility.h"
#include "gestureenddecisions.h"
#include "windowcontexthold.h"
#include "compositor/effectlogging.h"

#include <effect/effecthandler.h>
#include <virtualdesktops.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <utility>

#include "handlers/dragtracker.h"
#include "handlers/screenchangehandler.h"

namespace PlasmaZones {

bool PlasmaZonesEffect::holdContextEdit(KWin::EffectWindow* window, ResizeHold::ContextAxis axis)
{
    KWin::Window* const kw = window->window();
    WindowContextHold::Inputs in;
    in.dragLatched = m_contextHold.dragLatched.contains(window);
    in.outputInFlight = kw && kw->moveResizeOutput() != kw->output();
    if (const auto stamp = m_contextStampPerWindow.constFind(window); stamp != m_contextStampPerWindow.cend()) {
        // Judged on the stamp, which no held edit has advanced, and on the
        // desktop the COMMITTED output shows: whether the window was in view
        // where it was before KWin moved it (F711).
        const KWin::VirtualDesktop* const shown = desktopShownOn(window);
        in.inViewByStamp = shown && WindowContextEdge::covers(stamp->desktops, shown->id())
            && WindowContextEdge::covers(stamp->activities, KWin::effects->currentActivity());
    }
    in.crossingAwaitsSettle = m_screenChangeHandler->crossingAwaitsSettle(window);
    const quint8 reasons = WindowContextHold::reasonsFor(in);
    auto it = m_contextHold.holds.find(window);
    // An edit landing while an older hold stands is held with it even when its
    // own condition no longer reads true: the stamp it would be classified
    // against is the one the hold keeps.
    if (reasons == 0 && it == m_contextHold.holds.end()) {
        return false;
    }
    if (it == m_contextHold.holds.end()) {
        it = m_contextHold.holds.insert(window, ContextHold{});
    }
    if ((reasons & WindowContextHold::OutputInFlight) && !(it->reasons & WindowContextHold::OutputInFlight)) {
        it->committedAtHold = kw->output();
        it->generation = ++m_contextHold.seq;
        // A client that never commits the move must not hold the window: at
        // the deadline the edit runs against the live state (F687).
        QTimer::singleShot(GestureEndDecisions::kAckDeadlineMs, this,
                           [this, safeW = QPointer<KWin::EffectWindow>(window), generation = it->generation]() {
                               if (!safeW) {
                                   return;
                               }
                               const auto hold = m_contextHold.holds.constFind(safeW.data());
                               if (hold != m_contextHold.holds.cend() && hold->generation == generation) {
                                   releaseContextHold(safeW, WindowContextHold::OutputInFlight);
                               }
                           });
    }
    it->reasons |= reasons;
    it->heldAxes |= axis;
    qCDebug(lcEffect) << "Context edit held for" << getWindowId(window) << "axis" << int(axis) << "reasons"
                      << int(it->reasons);
    return true;
}

void PlasmaZonesEffect::releaseContextHold(KWin::EffectWindow* w, quint8 reasons)
{
    const auto it = m_contextHold.holds.find(w);
    if (it == m_contextHold.holds.end()) {
        return;
    }
    it->reasons &= static_cast<quint8>(~reasons);
    if (it->reasons != 0) {
        return;
    }
    const quint8 heldAxes = it->heldAxes;
    // Erased FIRST, as the resize drain releases: the bodies below must not
    // see a hold.
    m_contextHold.holds.erase(it);
    if (heldAxes == 0 || w->isDeleted()) {
        return;
    }
    // A resize that began meanwhile runs the edits from its own drain, after
    // its crossing.
    if (m_resizeHold.window == w) {
        m_resizeHold.heldAxes |= heldAxes;
        return;
    }
    qCDebug(lcEffect) << "Context hold released for" << getWindowId(w) << "axes" << int(heldAxes);
    // Each edit pushes the live context first (F293), then classifies it
    // against the held stamp from the output the window is on now, so a move
    // KWin took back is an empty diff and runs no arm.
    if (heldAxes & ResizeHold::DesktopAxis) {
        runContextEdit(w, ResizeHold::DesktopAxis);
    }
    if (heldAxes & ResizeHold::ActivityAxis) {
        runContextEdit(w, ResizeHold::ActivityAxis);
    }
    // The rule invalidation the metadata handler skipped for a held edit.
    invalidateRuleCacheForStateChange(getWindowId(w));
}

void PlasmaZonesEffect::releaseDragContextHold(KWin::EffectWindow* w)
{
    if (!w || !m_contextHold.dragLatched.contains(w)) {
        return;
    }
    // A successor drag of the same window owns the latch now, and its own exit
    // releases it (the rule drainDragSuppressedRuleInvalidations keeps too).
    if (m_dragTracker->isDragging() && m_dragTracker->draggedWindow() == w) {
        return;
    }
    // KWin's move outlives the drop while another button is held. The tail's
    // drain releases the hold when the move ends (drainMoveTail), so the edits
    // KWin made during the tail are held too and run after its crossing.
    if (m_dragActivation.tail.window == w || w->isUserMove()) {
        return;
    }
    m_contextHold.dragLatched.remove(w);
    releaseContextHold(w, WindowContextHold::DragSubject);
}

WindowContextFields PlasmaZonesEffect::stampContextFields(KWin::EffectWindow* w) const
{
    const auto stamp = m_contextStampPerWindow.constFind(w);
    if (stamp == m_contextStampPerWindow.cend()) {
        return liveContextFields(w);
    }
    // The shape liveContextFields builds. The stamp keeps constant desktop
    // ids, and their x11 numbers are read now, so a desktop KWin renumbered
    // during the hold is reported under its current number. A set has no
    // order, so both lists go ascending.
    WindowContextFields fields;
    QList<int> numbers;
    const QList<KWin::VirtualDesktop*> all = KWin::effects->desktops();
    for (const KWin::VirtualDesktop* vd : all) {
        if (vd && stamp->desktops.contains(vd->id())) {
            numbers.append(static_cast<int>(vd->x11DesktopNumber()));
        }
    }
    std::sort(numbers.begin(), numbers.end());
    if (!numbers.isEmpty()) {
        fields.virtualDesktop = numbers.first();
    }
    if (numbers.size() > 1) {
        for (const int number : std::as_const(numbers)) {
            fields.desktops.append(number);
        }
    }
    QStringList activities(stamp->activities.cbegin(), stamp->activities.cend());
    activities.sort();
    fields.activity = activities.isEmpty() ? QString() : activities.first();
    if (activities.size() > 1) {
        for (const QString& activityId : std::as_const(activities)) {
            fields.activities.append(activityId);
        }
    }
    return fields;
}

} // namespace PlasmaZones
