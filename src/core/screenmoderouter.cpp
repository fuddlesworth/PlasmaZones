// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmoderouter.h"

#include "../autotile/AutotileEngine.h"
#include "../snap/SnapEngine.h"
#include "iwindowengine.h"
#include "layoutmanager.h"

namespace PlasmaZones {

ScreenModeRouter::ScreenModeRouter(LayoutManager* layoutManager, SnapEngine* snapEngine, AutotileEngine* autotileEngine)
    : m_layoutManager(layoutManager)
    , m_snapEngine(snapEngine)
    , m_autotileEngine(autotileEngine)
{
}

void ScreenModeRouter::setAutotileEngine(AutotileEngine* autotileEngine)
{
    m_autotileEngine = autotileEngine;
}

void ScreenModeRouter::setSnapEngine(SnapEngine* snapEngine)
{
    m_snapEngine = snapEngine;
}

AssignmentEntry::Mode ScreenModeRouter::modeFor(const QString& screenId) const
{
    // Prefer the autotile engine's live set: it reflects the actual
    // runtime state including per-screen overrides that the layout
    // manager's cascade doesn't know about. Fall back to the layout
    // manager for screens the engine hasn't seen yet (early startup,
    // screens with assignments but no live autotile state).
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screenId)) {
        return AssignmentEntry::Autotile;
    }
    if (m_layoutManager) {
        const int desktop = m_layoutManager->currentVirtualDesktop();
        const QString activity = m_layoutManager->currentActivity();
        const auto mode = m_layoutManager->modeForScreen(screenId, desktop, activity);
        // Guard against stale layout-manager state: if the layout cascade
        // says Autotile but the engine doesn't have a live state for this
        // screen, trust the engine — the cascade may still reflect a stale
        // assignment during mode transitions.
        if (mode == AssignmentEntry::Autotile && m_autotileEngine && !m_autotileEngine->isAutotileScreen(screenId)) {
            return AssignmentEntry::Snapping;
        }
        return mode;
    }
    return AssignmentEntry::Snapping;
}

IWindowEngine* ScreenModeRouter::engineFor(const QString& screenId) const
{
    switch (modeFor(screenId)) {
    case AssignmentEntry::Autotile:
        return m_autotileEngine;
    case AssignmentEntry::Snapping:
        return m_snapEngine;
    }
    return nullptr;
}

bool ScreenModeRouter::isSnapMode(const QString& screenId) const
{
    return modeFor(screenId) == AssignmentEntry::Snapping;
}

bool ScreenModeRouter::isAutotileMode(const QString& screenId) const
{
    return modeFor(screenId) == AssignmentEntry::Autotile;
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
