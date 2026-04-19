// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmoderouter.h"

#include "../autotile/AutotileEngine.h"
#include "../snap/SnapEngine.h"
#include "inavigationactions.h"
#include "iwindowengine.h"

#include <PhosphorZones/LayoutManager.h>

namespace PlasmaZones {

ScreenModeRouter::ScreenModeRouter(PhosphorZones::LayoutManager* layoutManager, SnapEngine* snapEngine,
                                   AutotileEngine* autotileEngine)
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
    if (m_autotileEngine->isAutotileScreen(screenId)) {
        return PhosphorZones::AssignmentEntry::Autotile;
    }
    const int desktop = m_layoutManager->currentVirtualDesktop();
    const QString activity = m_layoutManager->currentActivity();
    const auto mode = m_layoutManager->modeForScreen(screenId, desktop, activity);
    // Engine already confirmed "not autotile" at the top of this function,
    // so if the layout cascade still reports Autotile we're looking at
    // stale assignment state during a mode transition — trust the engine
    // and downgrade to Snapping. (A second isAutotileScreen check here
    // would be dead code given the early return above.)
    if (mode == PhosphorZones::AssignmentEntry::Autotile) {
        return PhosphorZones::AssignmentEntry::Snapping;
    }
    return mode;
}

IEngineLifecycle* ScreenModeRouter::engineFor(const QString& screenId) const
{
    switch (modeFor(screenId)) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return m_autotileEngine;
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

void ScreenModeRouter::setNavigationAdapters(INavigationActions* snapNavigator, INavigationActions* autotileNavigator)
{
    m_snapNavigator = snapNavigator;
    m_autotileNavigator = autotileNavigator;
}

INavigationActions* ScreenModeRouter::navigatorFor(const QString& screenId) const
{
    switch (modeFor(screenId)) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return m_autotileNavigator;
    case PhosphorZones::AssignmentEntry::Snapping:
        return m_snapNavigator;
    }
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
        if (isAutotileMode(sid)) {
            out.autotile.append(sid);
        } else {
            out.snap.append(sid);
        }
    }
    return out;
}

} // namespace PlasmaZones
