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
    // no useful semantics — the resolver has nothing to dispatch to. qFatal
    // fires in BOTH debug and release builds (no Q_ASSERT layered on top —
    // a combined Q_ASSERT_X here would print a generic message that
    // doesn't identify WHICH pointer was null, defeating the diagnostic
    // intent). The detailed qFatal printf below names every axis so a
    // wiring bug is immediately attributable.
    if (!m_workspaceState || !m_modeProvider || !m_gateSource) {
        qFatal(
            "PhosphorContext::ContextResolver: null adapter pointer "
            "(workspaceState=%p modeProvider=%p gateSource=%p)",
            static_cast<void*>(m_workspaceState), static_cast<void*>(m_modeProvider), static_cast<void*>(m_gateSource));
        // qFatal lacks [[noreturn]] on MSVC's QMessageLogger::fatal —
        // mirror snapadaptor.cpp's defensive `return` so a future
        // maintainer adding statements after this block can't have them
        // silently execute on MSVC after a null-pointer wiring bug.
        return;
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
    handle.virtualDesktop = m_workspaceState->currentVirtualDesktopForScreen(screenId);
    handle.activity = m_workspaceState->currentActivity();
    handle.mode = m_modeProvider->modeFor(screenId);
    return handle;
}

ContextHandle ContextResolver::globalHandle() const
{
    // No-screen handle for adaptors that only need to gate on (desktop,
    // activity). The mode falls through to the `IModeProvider`'s empty-
    // screen contract (documented at `IContextInputs.h::IModeProvider::modeFor`
    // as returning the default mode for an empty `screenId`), keeping the
    // "default mode" decision in one place — `IModeProvider` implementations
    // — rather than duplicating the fallback here. Callers that need to
    // gate on every mode iterate `PhosphorZones::allModes()` and call
    // `handleForMode` per mode.
    ContextHandle handle;
    handle.screenId = QString();
    handle.virtualDesktop = m_workspaceState->currentVirtualDesktop();
    handle.activity = m_workspaceState->currentActivity();
    handle.mode = m_modeProvider->modeFor(QString());
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
    handle.virtualDesktop = m_workspaceState->currentVirtualDesktopForScreen(screenId);
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
    //
    // The desktop value is a system-boundary input (read from JSON
    // without prior validation). Clamp strictly-negative values to 0;
    // positive values pass through. `0` is the "pinned across all
    // desktops" sentinel that the IContextGateSource adapter's own
    // `<= 0` short-circuit treats as "skip the per-desktop check"
    // (see IContextInputs.h IContextGateSource contract). A negative
    // value would survive into that path and is undefined; clamping
    // here means a hand-edited file with a negative desktop reads as
    // "pinned" rather than reaching the settings store with a value
    // its API does not document.
    ContextHandle handle;
    handle.screenId = screenId;
    handle.virtualDesktop = virtualDesktop < 0 ? 0 : virtualDesktop;
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
