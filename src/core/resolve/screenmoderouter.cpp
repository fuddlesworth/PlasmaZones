// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmoderouter.h"

#include <PhosphorZones/LayoutRegistry.h>

#include <QtGlobal>

namespace PlasmaZones {

ScreenModeRouter::ScreenModeRouter(PhosphorZones::LayoutRegistry* layoutManager,
                                   PhosphorEngine::IPlacementEngine* snapEngine,
                                   PhosphorEngine::IPlacementEngine* autotileEngine,
                                   PhosphorEngine::IPlacementEngine* scrollEngine)
    : m_layoutManager(layoutManager)
    , m_snapEngine(snapEngine)
    , m_autotileEngine(autotileEngine)
    , m_scrollEngine(scrollEngine)
{
    // qFatal aborts unambiguously in both debug and release builds.
    // modeFor() / partitionByMode() unconditionally deref each pointer,
    // so a null dependency is a wiring bug that crashes on the first
    // call — escalate at construction so the failure is loud and
    // attributable to the construction site, not the first user.
    if (!layoutManager || !snapEngine || !autotileEngine || !scrollEngine) {
        qFatal(
            "ScreenModeRouter: null dependency at construction "
            "(layoutManager=%p, snapEngine=%p, autotileEngine=%p, scrollEngine=%p) — daemon-wiring bug",
            static_cast<void*>(layoutManager), static_cast<void*>(snapEngine), static_cast<void*>(autotileEngine),
            static_cast<void*>(scrollEngine));
    }
}

PhosphorZones::AssignmentEntry::Mode ScreenModeRouter::modeFor(const QString& screenId) const
{
    // An empty id can never name an engine-managed screen; answering
    // Snapping directly makes the contract a guard rather than an accident
    // of what the engine sets happen to (not) contain.
    if (screenId.isEmpty()) {
        return PhosphorZones::AssignmentEntry::Snapping;
    }
    // Prefer the autotile engine's live set: it reflects the actual
    // runtime state including per-screen overrides that the layout
    // manager's cascade doesn't know about. Fall back to the layout
    // manager for screens the engine hasn't seen yet.
    if (m_autotileEngine->isActiveOnScreen(screenId)) {
        return PhosphorZones::AssignmentEntry::Autotile;
    }
    // Same live-set-first rationale for the scrolling engine: its active
    // set reflects the applied assignment including any transition the
    // cascade hasn't caught up with yet.
    if (m_scrollEngine->isActiveOnScreen(screenId)) {
        return PhosphorZones::AssignmentEntry::Scrolling;
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
    // synchronous transition inside `updateEngineScreens`
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
    // The same downgrade applies to Scrolling: both live-set engines were
    // checked above, so a cascade that still reports one of them is stale
    // assignment state mid-transition — trust the engines and fall back
    // to Snapping.
    // One cascade-Autotile shape is NOT stale transition state: the explicit
    // algorithm opt-out ("autotile:none"). updateEngineScreens deliberately
    // leaves that screen out of the engine set FOREVER, so "engine inactive +
    // cascade Autotile" is its permanent steady state, and downgrading it to
    // Snapping makes every mode-keyed surface (mode toggle, OSD gating,
    // cheatsheet, snap partitioning) treat an opted-out tiling screen as a
    // live snapping screen. The declared mode is the user's standing choice —
    // honor it. Scoped strictly to the sentinel: a bare-Autotile context with
    // an empty or concrete algorithm and an inactive engine keeps the
    // downgrade (that shape ships as the suppressed-default behavior and
    // genuinely is transitional or suppression-driven).
    if (mode == PhosphorZones::AssignmentEntry::Autotile
        && m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, activity) == PhosphorZones::NoTilingAlgorithm) {
        return PhosphorZones::AssignmentEntry::Autotile;
    }
    if (mode == PhosphorZones::AssignmentEntry::Autotile || mode == PhosphorZones::AssignmentEntry::Scrolling) {
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
        return m_scrollEngine;
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

bool ScreenModeRouter::isScrollingMode(const QString& screenId) const
{
    return modeFor(screenId) == PhosphorZones::AssignmentEntry::Scrolling;
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
            out.scrolling.append(sid);
            break;
        }
    }
    return out;
}

} // namespace PlasmaZones
