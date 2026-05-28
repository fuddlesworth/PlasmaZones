// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorContext/ContextResolver.h>

#include <QtGlobal>

namespace PhosphorContext {

ContextResolver::ContextResolver(IWorkspaceState* workspaceState, IModeProvider* modeProvider,
                                 IContextGateSource* gateSource)
    : m_workspaceState(workspaceState)
    , m_modeProvider(modeProvider)
    , m_gateSource(gateSource)
{
    // All three adapters are documented preconditions; passing nullptr has
    // no useful semantics — the resolver has nothing to dispatch to.
    // Assert in debug + qFatal in release so a wiring bug crashes loudly at
    // construction rather than silently returning zeroes from every query.
    Q_ASSERT_X(m_workspaceState != nullptr, "PhosphorContext::ContextResolver", "IWorkspaceState* must not be null");
    Q_ASSERT_X(m_modeProvider != nullptr, "PhosphorContext::ContextResolver", "IModeProvider* must not be null");
    Q_ASSERT_X(m_gateSource != nullptr, "PhosphorContext::ContextResolver", "IContextGateSource* must not be null");
    if (!m_workspaceState || !m_modeProvider || !m_gateSource) {
        qFatal(
            "PhosphorContext::ContextResolver: null adapter pointer "
            "(workspaceState=%p modeProvider=%p gateSource=%p)",
            static_cast<void*>(m_workspaceState), static_cast<void*>(m_modeProvider), static_cast<void*>(m_gateSource));
    }
}

ContextHandle ContextResolver::handleFor(const QString& screenId) const
{
    // Snapshot all three axes once. Reading workspace state and mode in
    // two separate calls inside one event-loop tick is safe — neither the
    // virtual-desktop manager nor the activity manager nor the mode
    // router can fire a state-changed signal mid-call (they are
    // QObject-based services on the same thread as the resolver and any
    // observable change comes through their `*Changed` signals that the
    // GUI thread can only process between event-loop ticks).
    ContextHandle handle;
    handle.screenId = screenId;
    handle.virtualDesktop = m_workspaceState->currentVirtualDesktop();
    handle.activity = m_workspaceState->currentActivity();
    handle.mode = m_modeProvider->modeFor(screenId);
    return handle;
}

ContextHandle ContextResolver::globalHandle() const
{
    // No-screen handle for adaptors that only need to gate on (desktop,
    // activity). Mode falls back to Snapping per the documented "no
    // screen ↔ default mode" contract. Callers that need both legs of
    // the lock check (snap + autotile) explicitly use two `handleForMode`
    // builds — see `Daemon::isCurrentContextLocked` for the canonical
    // pattern.
    ContextHandle handle;
    handle.virtualDesktop = m_workspaceState->currentVirtualDesktop();
    handle.activity = m_workspaceState->currentActivity();
    handle.mode = PhosphorZones::AssignmentEntry::Snapping;
    return handle;
}

ContextHandle ContextResolver::handleForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const
{
    // Specialised entry point for handlers that already know the mode
    // they want — typically an autotile-only shortcut handler. Skips
    // the mode provider call (the only reason callers would prefer this
    // overload) but otherwise mirrors handleFor's snapshot semantics.
    ContextHandle handle;
    handle.screenId = screenId;
    handle.virtualDesktop = m_workspaceState->currentVirtualDesktop();
    handle.activity = m_workspaceState->currentActivity();
    handle.mode = mode;
    return handle;
}

ContextHandle ContextResolver::handleForPersisted(const QString& screenId, int virtualDesktop,
                                                  const QString& activity) const
{
    // Persisted desktop/activity come from disk; mode is the screen's
    // CURRENT routing (so a persisted snap entry on a screen now in
    // autotile mode reads against the autotile disable list, matching
    // the historical `m_screenModeRouter->modeFor(screenId)` lookup in
    // saveload.cpp).
    ContextHandle handle;
    handle.screenId = screenId;
    handle.virtualDesktop = virtualDesktop;
    handle.activity = activity;
    handle.mode = m_modeProvider->modeFor(screenId);
    return handle;
}

int ContextResolver::currentVirtualDesktop() const
{
    return m_workspaceState->currentVirtualDesktop();
}

QString ContextResolver::currentActivity() const
{
    return m_workspaceState->currentActivity();
}

DisabledReason ContextResolver::disabledReason(const ContextHandle& handle) const
{
    // Walk Monitor → Desktop → Activity in priority order. The first
    // tripped leg wins; subsequent legs are short-circuited so a
    // monitor-disabled screen never falsely reports as
    // ActivityDisabled. This matches the historical cascade in
    // `core/settings_interfaces.h::contextDisabledReason`.
    if (m_gateSource->isMonitorDisabled(handle.mode, handle.screenId)) {
        return DisabledReason::MonitorDisabled;
    }
    if (m_gateSource->isDesktopDisabled(handle.mode, handle.screenId, handle.virtualDesktop)) {
        return DisabledReason::DesktopDisabled;
    }
    if (m_gateSource->isActivityDisabled(handle.mode, handle.screenId, handle.activity)) {
        return DisabledReason::ActivityDisabled;
    }
    return DisabledReason::NotDisabled;
}

bool ContextResolver::isLocked(const ContextHandle& handle) const
{
    return m_gateSource->isContextLocked(handle.mode, handle.screenId, handle.virtualDesktop, handle.activity);
}

} // namespace PhosphorContext
