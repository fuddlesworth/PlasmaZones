// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmoderouter.h"

#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

ScreenModeRouter::ScreenModeRouter(PhosphorZones::LayoutRegistry* layoutManager,
                                   PhosphorEngine::IPlacementEngine* snapEngine,
                                   PhosphorEngine::IPlacementEngine* autotileEngine)
    : m_layoutManager(layoutManager)
    , m_snapEngine(snapEngine)
    , m_autotileEngine(autotileEngine)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(snapEngine);
    Q_ASSERT(autotileEngine);
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
    const int desktop = m_layoutManager->currentVirtualDesktop();
    const QString activity = m_layoutManager->currentActivity();
    const auto mode = m_layoutManager->modeForScreen(screenId, desktop, activity);
    // Engine already confirmed "not autotile" at the top of this function,
    // so if the layout cascade still reports Autotile we're looking at
    // stale assignment state during a mode transition — trust the engine
    // and downgrade to Snapping. (A second isActiveOnScreen check here
    // would be dead code given the early return above.)
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
    // runtime. Q_UNREACHABLE + nullptr is the safe fallback if that happens.
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
