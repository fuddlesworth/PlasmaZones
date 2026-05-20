// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmoderouter.h"

#include <PhosphorZones/LayoutRegistry.h>

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
    Q_ASSERT(layoutManager);
    Q_ASSERT(snapEngine);
    Q_ASSERT(autotileEngine);
    Q_ASSERT(scrollEngine);
}

PhosphorZones::AssignmentEntry::Mode ScreenModeRouter::modeFor(const QString& screenId) const
{
    // Prefer the engines' live sets: they reflect the actual runtime state
    // including per-screen overrides that the layout manager's cascade
    // doesn't know about. Fall back to the layout manager for screens the
    // engines haven't seen yet.
    //
    // Mutual exclusion: a screen can be active in autotile OR scroll, never
    // both. Updating the active set is the responsibility of
    // updateAutotileScreens / updateScrollScreens; if both ever flag the same
    // screen we silently let autotile win below — fail loudly in debug builds
    // so the underlying bug surfaces immediately.
    const bool autotileActive = m_autotileEngine->isActiveOnScreen(screenId);
    const bool scrollActive = m_scrollEngine->isActiveOnScreen(screenId);
    Q_ASSERT_X(!(autotileActive && scrollActive), "ScreenModeRouter::modeFor",
               "screen reported active in both autotile and scroll engines");
    if (autotileActive) {
        return PhosphorZones::AssignmentEntry::Autotile;
    }
    if (scrollActive) {
        return PhosphorZones::AssignmentEntry::Scroll;
    }
    const int desktop = m_layoutManager->currentVirtualDesktop();
    const QString activity = m_layoutManager->currentActivity();
    const auto mode = m_layoutManager->modeForScreen(screenId, desktop, activity);
    // The engine live-set checks above already confirmed "not autotile" and
    // "not scroll". If the layout cascade still reports one of those modes
    // we're looking at stale assignment state during a mode transition —
    // trust the engines and downgrade to Snapping.
    if (mode == PhosphorZones::AssignmentEntry::Autotile || mode == PhosphorZones::AssignmentEntry::Scroll) {
        return PhosphorZones::AssignmentEntry::Snapping;
    }
    return mode;
}

PhosphorEngine::IPlacementEngine* ScreenModeRouter::engineFor(const QString& screenId) const
{
    switch (modeFor(screenId)) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return m_autotileEngine;
    case PhosphorZones::AssignmentEntry::Scroll:
        return m_scrollEngine;
    case PhosphorZones::AssignmentEntry::Snapping:
        return m_snapEngine;
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

bool ScreenModeRouter::isScrollMode(const QString& screenId) const
{
    return modeFor(screenId) == PhosphorZones::AssignmentEntry::Scroll;
}

ScreenModeRouter::Partitioned ScreenModeRouter::partitionByMode(const QStringList& screenIds) const
{
    Partitioned out;
    for (const QString& sid : screenIds) {
        switch (modeFor(sid)) {
        case PhosphorZones::AssignmentEntry::Autotile:
            out.autotile.append(sid);
            break;
        case PhosphorZones::AssignmentEntry::Scroll:
            out.scroll.append(sid);
            break;
        case PhosphorZones::AssignmentEntry::Snapping:
            out.snap.append(sid);
            break;
        }
    }
    return out;
}

} // namespace PlasmaZones
