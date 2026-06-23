// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmoderouter.h"

#include <PhosphorZones/LayoutRegistry.h>

#include <QtGlobal>

namespace PlasmaZones {

ScreenModeRouter::ScreenModeRouter(PhosphorZones::LayoutRegistry* layoutManager,
                                   PhosphorEngine::IPlacementEngine* snapEngine,
                                   PhosphorEngine::IPlacementEngine* autotileEngine)
    : m_layoutManager(layoutManager)
    , m_snapEngine(snapEngine)
    , m_autotileEngine(autotileEngine)
{
    // qFatal aborts unambiguously in both debug and release builds.
    // modeFor() / partitionByMode() unconditionally deref each pointer,
    // so a null dependency is a wiring bug that crashes on the first
    // call — escalate at construction so the failure is loud and
    // attributable to the construction site, not the first user.
    if (!layoutManager || !snapEngine || !autotileEngine) {
        qFatal(
            "ScreenModeRouter: null dependency at construction "
            "(layoutManager=%p, snapEngine=%p, autotileEngine=%p) — daemon-wiring bug",
            static_cast<void*>(layoutManager), static_cast<void*>(snapEngine), static_cast<void*>(autotileEngine));
    }
}

PhosphorZones::AssignmentEntry::Mode ScreenModeRouter::modeFor(const QString& screenId) const
{
    // Prefer the autotile engine's live set: it reflects the actual
    // runtime state including per-screen overrides that the layout
    // manager's cascade doesn't know about. Fall back to the layout
    // manager for screens the engine hasn't seen yet.
    if (m_autotileEngine->isActiveOnScreen(screenId)) {
        return PhosphorZones::AssignmentEntry::Autotile;
    }
    const int desktop = m_layoutManager->currentVirtualDesktopForScreen(screenId);
    const QString activity = m_layoutManager->currentActivity();
    const auto mode = m_layoutManager->modeForScreen(screenId, desktop, activity);
    // Engine already confirmed "not autotile" at the top of this function,
    // so if the layout cascade still reports Autotile we're looking at
    // stale assignment state during a mode transition — trust the engine
    // and downgrade to Snapping. (A second isActiveOnScreen check here
    // would be dead code given the early return above.)
    //
    // Asymmetry note (intentional): we downgrade engine-inactive +
    // cascade-Autotile to Snapping, but we do NOT downgrade the reverse
    // direction (engine-active overrides whatever the cascade says by
    // virtue of the early-return above). The only window where the
    // cascade could be Autotile-ahead of the engine is during the
    // synchronous transition inside `updateAutotileScreens`
    // (daemon/autotile.cpp), which calls `setActiveScreens` BEFORE the
    // user can fire a navigation shortcut on the new screen — the daemon
    // signal pump is single-threaded and processes assign-then-input
    // events in order. Cross-VS D-Bus queries from the kwin-effect ride
    // the bus's monotonic delivery order, so they see the post-
    // setActiveScreens snapshot too. Adding a symmetric "engine inactive
    // but cascade pending → trust cascade" branch would have to
    // distinguish "stale assignment" from "fresh assignment not yet
    // applied" — which the router has no signal for.
    //
    // Scrolling reports pass through unchanged: there is no engine to
    // cross-check (engineFor returns nullptr for Scrolling), so the
    // cascade is authoritative for the no-engine modes. Only Autotile
    // needs the engine cross-check because it's the one whose live
    // state can diverge from a stale assignment.
    if (mode == PhosphorZones::AssignmentEntry::Autotile) {
        return PhosphorZones::AssignmentEntry::Snapping;
    }
    return mode;
}

PhosphorEngine::IPlacementEngine* ScreenModeRouter::engineFor(const QString& screenId) const
{
    switch (modeFor(screenId)) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return m_autotileEngine;
    case PhosphorZones::AssignmentEntry::Snapping:
        return m_snapEngine;
    case PhosphorZones::AssignmentEntry::Scrolling:
        // No scrolling engine is wired yet — the assignment is honoured
        // by leaving the screen unmanaged (KWin's native placement runs
        // unimpeded). Returning nullptr matches the existing contract:
        // navigation.cpp / drag pipelines null-check the engine pointer
        // and treat null as "no managed window-placement here".
        return nullptr;
    }
    // Switch above is exhaustive over PhosphorZones::AssignmentEntry::Mode. Deliberately
    // no `default:` case so that adding a new enum value triggers -Wswitch
    // at compile time instead of silently falling through to nullptr at
    // runtime. The trailing Q_UNREACHABLE + return silences -Wreturn-type;
    // a path that reaches here means the enum gained a value the switch
    // didn't handle, which is a build-time omission, not a runtime
    // condition. Don't read this as a defensive guarantee — the path is
    // genuinely unreachable as long as the switch stays exhaustive.
    Q_UNREACHABLE();
    return nullptr;
}

bool ScreenModeRouter::isSnapMode(const QString& screenId) const
{
    return modeFor(screenId) == PhosphorZones::AssignmentEntry::Snapping;
}

bool ScreenModeRouter::isAutotileMode(const QString& screenId) const
{
    return modeFor(screenId) == PhosphorZones::AssignmentEntry::Autotile;
}

ScreenModeRouter::Partitioned ScreenModeRouter::partitionByMode(const QStringList& screenIds) const
{
    Partitioned out;
    for (const QString& sid : screenIds) {
        // Switch instead of isSnapMode/isAutotileMode pair so adding a
        // future mode produces a -Wswitch diagnostic here rather than
        // silently bucketing the new mode into `snap` (the original
        // pre-Scrolling default).
        switch (modeFor(sid)) {
        case PhosphorZones::AssignmentEntry::Autotile:
            out.autotile.append(sid);
            break;
        case PhosphorZones::AssignmentEntry::Snapping:
            out.snap.append(sid);
            break;
        case PhosphorZones::AssignmentEntry::Scrolling:
            out.passthrough.append(sid);
            break;
        }
    }
    return out;
}

} // namespace PlasmaZones
