// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorContext/IContextInputs.h>

#include <QString>

namespace PhosphorWorkspaces {
class ActivityManager;
class VirtualDesktopManager;
} // namespace PhosphorWorkspaces

namespace PhosphorZones {
class LayoutRegistry;
} // namespace PhosphorZones

namespace PlasmaZones {

class ISettings;
class ScreenModeRouter;

/**
 * @file contextresolverwiring.h
 * @brief Concrete daemon-side adapters wiring the existing services into
 *        the LGPL `PhosphorContext::IContextResolver` triple.
 *
 * The three adapters here are tiny one-to-one shims that let us keep the
 * `phosphor-context-resolver` library free of any PlasmaZones-specific
 * types (it sees only `AssignmentEntry::Mode`, `QString`, `int`) while
 * still routing every public call through the project's existing service
 * objects. Daemon owns one instance of each + one `ContextResolver`
 * constructed over them; D-Bus adaptors and the overlay service borrow
 * the resolver via `Daemon::contextResolver()`.
 *
 * Lifetime: each adapter holds raw non-owning pointers to its backing
 * services. Construct after the services exist, destroy before they
 * tear down. Daemon's declaration order takes care of that — see the
 * `m_contextResolver` member's position in daemon.h.
 */

/**
 * @brief `IWorkspaceState` impl over the daemon's VirtualDesktopManager +
 *        ActivityManager.
 *
 * Defaults match `Daemon::currentDesktop()` / `currentActivity()` exactly
 * (returns `0` and the empty string respectively when either manager is
 * null), so swapping a consumer from the direct daemon getters to the
 * adapter is observationally identical.
 */
class DaemonWorkspaceStateAdapter : public PhosphorContext::IWorkspaceState
{
public:
    DaemonWorkspaceStateAdapter(PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager,
                                PhosphorWorkspaces::ActivityManager* activityManager);
    ~DaemonWorkspaceStateAdapter() override = default;

    int currentVirtualDesktop() const override;
    int currentVirtualDesktopForScreen(const QString& screenId) const override;
    QString currentActivity() const override;

private:
    // Raw non-owning pointers — the daemon's declaration order
    // (services first, adapters second, resolver third) guarantees the
    // adapters tear down before their backing services.
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager;
    PhosphorWorkspaces::ActivityManager* m_activityManager;
};

/**
 * @brief `IModeProvider` impl over the daemon's `ScreenModeRouter`.
 *
 * One-line forwarder. The router pointer is captured at construction and
 * never mutated post-construction (no setter exists). Construction with
 * a null router is permitted (headless tests) and the adapter falls back
 * to `AssignmentEntry::Snapping`, matching the inline
 * `m_screenModeRouter ? m_screenModeRouter->modeFor(...) : Snapping`
 * pattern the migrated call sites used. During the daemon's `stop()`
 * shutdown, the declared teardown order tears this adapter down before
 * the router itself is destroyed — the adapter never sees a router that
 * has already been freed.
 */
class DaemonScreenModeAdapter : public PhosphorContext::IModeProvider
{
public:
    explicit DaemonScreenModeAdapter(ScreenModeRouter* router);
    ~DaemonScreenModeAdapter() override = default;

    PhosphorZones::AssignmentEntry::Mode modeFor(const QString& screenId) const override;

private:
    ScreenModeRouter* m_router; ///< Non-owning; outlives this adapter.
};

/**
 * @brief `IContextGateSource` impl over `ISettings` + the existing
 *        `contextLockKey` composition helper.
 *
 * The three disable predicates forward to the `IZoneVisualizationSettings`
 * sub-interface (which `ISettings` already implements). The lock predicate
 * composes the Mode into the screen-key string via `Utils::contextLockKey`
 * before calling `ISettings::isContextLocked`, mirroring the call shape
 * every migrated site used before (`isContextLocked(contextLockKey(modeInt,
 * screenId), desktop, activity)`), then ORs in the rule-driven lock resolved
 * by `LayoutRegistry::resolveContextLocked` so a `LockContext` window rule
 * locks the context live without ever touching the persisted lock store.
 *
 * Null `ISettings*` / `LayoutRegistry*` are permitted for headless-test
 * wiring; every predicate returns `false` in that case (the "no settings →
 * nothing is gated" fallback that the daemon's existing code already
 * exercises).
 */
class DaemonSettingsGateAdapter : public PhosphorContext::IContextGateSource
{
public:
    DaemonSettingsGateAdapter(ISettings* settings, PhosphorZones::LayoutRegistry* layoutRegistry);
    ~DaemonSettingsGateAdapter() override = default;

    bool isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId) const override;
    bool isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId,
                           int desktop) const override;
    bool isActivityDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId,
                            const QString& activity) const override;
    bool isContextLocked(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId, int desktop,
                         const QString& activity) const override;

private:
    ISettings* m_settings; ///< Non-owning; outlives this adapter.
    /// Non-owning; outlives this adapter (the daemon declares the registry
    /// before this adapter, so it tears down after). Source of the live,
    /// never-persisted rule-driven context lock OR-ed into `isContextLocked`.
    PhosphorZones::LayoutRegistry* m_layoutRegistry;
};

} // namespace PlasmaZones
