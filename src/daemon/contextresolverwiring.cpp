// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contextresolverwiring.h"

#include "../core/isettings.h"
#include "../core/screenmoderouter.h"
#include "../core/utils.h"

#include <PhosphorZones/LayoutRegistry.h>

#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

namespace PlasmaZones {

// ── DaemonWorkspaceStateAdapter ─────────────────────────────────────────

DaemonWorkspaceStateAdapter::DaemonWorkspaceStateAdapter(
    PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager,
    PhosphorWorkspaces::ActivityManager* activityManager)
    : m_virtualDesktopManager(virtualDesktopManager)
    , m_activityManager(activityManager)
{
}

int DaemonWorkspaceStateAdapter::currentVirtualDesktop() const
{
    // Mirror Daemon::currentDesktop() — return 0 when no VDM is wired so
    // adapter-routed gates report the same value as the historical inline
    // pattern. 0 in PZ's taxonomy is "pinned across all desktops" and is
    // skipped by isDesktopDisabled (see contextDisabledReason in
    // core/settings_interfaces.h).
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

QString DaemonWorkspaceStateAdapter::currentActivity() const
{
    // Guard on both the manager pointer AND `ActivityManager::isAvailable()`
    // so headless sessions report "" rather than crashing through a deref of a
    // manager whose D-Bus backend never connected. Shared with Daemon and WTA
    // via the ActivityManager helper.
    return PhosphorWorkspaces::ActivityManager::currentActivityOrEmpty(m_activityManager);
}

// ── DaemonScreenModeAdapter ─────────────────────────────────────────────

DaemonScreenModeAdapter::DaemonScreenModeAdapter(ScreenModeRouter* router)
    : m_router(router)
{
}

PhosphorZones::AssignmentEntry::Mode DaemonScreenModeAdapter::modeFor(const QString& screenId) const
{
    // Snapping is the documented "no router wired" fallback (matches the
    // inline `m_screenModeRouter ? router->modeFor(screenId) : Snapping`
    // pattern every migrated site used). The router itself is allowed to
    // return Snapping for an unknown screenId — we don't second-guess.
    return m_router ? m_router->modeFor(screenId) : PhosphorZones::AssignmentEntry::Snapping;
}

// ── DaemonSettingsGateAdapter ───────────────────────────────────────────

DaemonSettingsGateAdapter::DaemonSettingsGateAdapter(ISettings* settings, PhosphorZones::LayoutRegistry* layoutRegistry)
    : m_settings(settings)
    , m_layoutRegistry(layoutRegistry)
{
}

bool DaemonSettingsGateAdapter::isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode,
                                                  const QString& screenId) const
{
    return m_settings ? m_settings->isMonitorDisabled(mode, screenId) : false;
}

bool DaemonSettingsGateAdapter::isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId,
                                                  int desktop) const
{
    // Mirror the contextDisabledReason short-circuit at
    // core/settings_interfaces.h:428 — desktop 0 means "pinned across all
    // desktops" and must NOT match a per-desktop disable entry. Without
    // this guard the adapter would call into ISettings with desktop=0 and
    // potentially false-positive against a literal-zero stored entry.
    if (!m_settings || desktop <= 0)
        return false;
    return m_settings->isDesktopDisabled(mode, screenId, desktop);
}

bool DaemonSettingsGateAdapter::isActivityDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId,
                                                   const QString& activity) const
{
    // Same short-circuit shape — empty activity means "no AM wired" and
    // must not match a literal-empty entry. Mirrors
    // core/settings_interfaces.h:430.
    if (!m_settings || activity.isEmpty())
        return false;
    return m_settings->isActivityDisabled(mode, screenId, activity);
}

bool DaemonSettingsGateAdapter::isContextLocked(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId,
                                                int desktop, const QString& activity) const
{
    // Rule-driven lock first: a `LockContext` context rule locks the active
    // layout live, regardless of engine mode, and is never written to the
    // persisted lock store — so it OR-s with (never replaces) the manual
    // ToggleLayoutLock state below. Mode-agnostic: the rule query ignores the
    // Mode axis, so the same rule lock surfaces for whichever mode is asked.
    if (m_layoutRegistry && m_layoutRegistry->resolveContextLocked(screenId, desktop, activity))
        return true;

    if (!m_settings)
        return false;
    // Fold the Mode into the screen-key the way every pre-migration call
    // site did. Explicit cast over the unscoped-enum implicit conversion:
    // the wire format takes an int, and writing the cast keeps the call
    // safe if `AssignmentEntry::Mode` ever migrates to `enum class`.
    // The lock store is keyed by composite "mode:screenId" strings so
    // the Mode axis stays implicit on the wire format — changing that
    // is a schema migration, not an adapter concern.
    return m_settings->isContextLocked(Utils::contextLockKey(static_cast<int>(mode), screenId), desktop, activity);
}

} // namespace PlasmaZones
